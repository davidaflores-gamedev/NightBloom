//------------------------------------------------------------------------------
// GameApp.cpp
//
// The standalone Nightbloom Game runtime (Milestone 2, see .claude/PACKAGING_ROADMAP.md).
// Links the engine, no editor UI: it boots a project's start scene and renders it.
//
// M2a (this file): boots + renders a saved scene with a fly-cam (right-mouse to look).
// M2b (next): a Jolt-backed character controller walking on terrain — the guardrail.
//------------------------------------------------------------------------------

#include "Engine/Core/Application.hpp"
#include "Engine/Core/Scene.hpp"
#include "Engine/Core/SceneSerializer.hpp"
#include "Engine/Core/Project.hpp"
#include "Engine/Core/GameSettings.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include "Engine/Renderer/AssetManager.hpp"

#include <imgui.h>
#include <string>
#include "Engine/Renderer/Camera.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include "Engine/Renderer/Frustum.hpp"
#include "Engine/Input/InputSystem.hpp"
#include "Engine/Window/Window.hpp"
#include "Engine/Terrain/TerrainSystem.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Gameplay/Pawn.hpp"
#include "Engine/Gameplay/CharacterPawn.hpp"
#include "Engine/Gameplay/VehiclePawn.hpp"
#include "Engine/Gameplay/ThirdPersonCameraRig.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <cstdlib>  // __argc / __argv

namespace Nightbloom
{
	class GameApplication : public Application
	{
	public:
		GameApplication() : Application("Nightbloom Game") {}

		~GameApplication() override
		{
			// TerrainSystem holds renderer-owned GPU handles; free them while the
			// renderer (a base-class member, destroyed after this body runs) is alive.
			if (m_TerrainReady)
				m_Terrain.Shutdown();
		}

		void OnStartup() override
		{
			LOG_INFO("=== Nightbloom Game starting ===");

			m_Settings.Load();  // %APPDATA%/Nightbloom/game.json (defaults if absent)

			const float aspect = AspectRatio();

			m_Camera = std::make_unique<Camera>();
			m_Camera->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
			m_Camera->SetRotation(-135.0f, -20.0f);
			m_Camera->SetPerspectiveInfiniteReverseZ(45.0f, aspect, 0.1f);

			m_Scene = std::make_unique<Scene>();

			std::filesystem::path projectFile = ResolveProjectFile();
			if (!projectFile.empty() && Project::Load(projectFile, m_Project))
			{
				LOG_INFO("Game: project '{}'", m_Project.Settings().name);
				GetWindow()->SetTitle(m_Project.Settings().windowTitle);
				// Resolve scene asset paths against this project's Assets/ first (a
				// packaged build ships its assets there); falls back to global Assets/.
				AssetManager::Get().SetProjectAssetsPath(m_Project.AssetsPath().string());
				LoadStartScene();
			}
			else
			{
				LOG_ERROR("Game: could not load a project (looked for '{}'). "
					"Pass a .nbproj path as the first argument.", projectFile.string());
			}

			// The environment systems are editor-panel-owned and not serialized yet,
			// so the runtime instantiates its own terrain (hardcoded for now — M2b).
			// This is the ground the character controller will eventually walk on.
			InitTerrain();

			GetWindow()->SetResizeCallback([this](uint32_t w, uint32_t h) {
				if (w > 0 && h > 0 && m_Camera)
					m_Camera->SetPerspectiveInfiniteReverseZ(45.0f,
						static_cast<float>(w) / static_cast<float>(h), 0.1f);
			});

			// Push the saved video settings into the renderer/window before the first
			// frame (shadow quality, vsync, windowed resolution).
			ApplySettings();

			// Pawn system + third-person body (Phase 1). Possess an on-foot character;
			// the vehicle pawn slots in beside it in Phase 2.
			InitCharacter();

			// Grab the mouse for always-on mouselook (Esc opens the menu + frees it).
			GetWindow()->SetRelativeMouseMode(true);
		}

		// Push m_Settings into the renderer/window. Sensitivity + invert-Y are read
		// live in LookDelta(), so they need no explicit apply.
		void ApplySettings()
		{
			GetRenderer()->SetShadowResolution(GameSettings::ShadowResolutionFor(m_Settings.shadowQuality));
			GetRenderer()->SetVSync(m_Settings.vsync);
			if (GetWindow()->GetWidth() != m_Settings.width || GetWindow()->GetHeight() != m_Settings.height)
				GetWindow()->SetSize(m_Settings.width, m_Settings.height);
		}

		void OnUpdate(float deltaTime) override
		{
			UpdatePauseAndCapture();

			// Walk on the terrain (third-person) once it has CPU height data;
			// otherwise fall back to the free-fly camera (no visible body).
			const bool characterMode = (m_TerrainReady && m_Terrain.HasHeightData());
			if (characterMode)
				UpdateCharacterMode(deltaTime);
			else
				UpdateFlyCam(deltaTime);

			// The placeholder bodies only exist in character mode; hide them when the
			// fly-cam fallback is driving.
			if (SceneObject* body = SceneObjectAt(m_BodyIndex)) body->visible = characterMode;
			if (SceneObject* bike = SceneObjectAt(m_BikeIndex)) bike->visible = characterMode;

			GetRenderer()->SetViewMatrix(m_Camera->GetViewMatrix());
			GetRenderer()->SetProjectionMatrix(m_Camera->GetProjectionMatrix());
			GetRenderer()->SetCameraPosition(m_Camera->GetPosition());
			GetRenderer()->SetLightingData(m_Scene->BuildLightingData());
			if (m_Scene->GetLightCount() > 0)
				GetRenderer()->SetShadowConfig(m_Scene->GetLight(0)->shadowConfig);

			m_Scene->Update(deltaTime);
		}

		void OnRender() override
		{
			DrawList drawList;
			glm::mat4 viewProj = m_Camera->GetProjectionMatrix() * m_Camera->GetViewMatrix();
			Frustum frustum = Frustum::ExtractFromMatrix(viewProj);
			m_Scene->BuildDrawList(drawList, &frustum);

			if (m_TerrainReady && m_Terrain.IsReady())
			{
				m_Terrain.UpdateLOD(m_Camera->GetPosition(), kTerrainResolution);
				m_Terrain.SubmitDraw(drawList);
			}

			drawList.SortByPipeline();
			GetRenderer()->SubmitDrawList(drawList);

			// ImGui is already inside a frame (Renderer::BeginFrame ran NewFrame) and
			// gets rendered in the post-process pass — so the menu is just widgets here.
			if (m_MenuOpen)
				DrawPauseMenu();
			else
				DrawInteractPrompt();
		}

	private:
		// Esc toggles the pause/settings menu. While the menu is open the cursor is
		// freed (for clicking widgets); while it's closed we play with always-on
		// mouselook (captured cursor). Quit is a menu button now, not Esc.
		void UpdatePauseAndCapture()
		{
			Window* win = GetWindow();

			if (GetInput()->IsPressed(InputCode::Key_Escape))
				SetMenuOpen(!m_MenuOpen);

			if (m_MenuOpen)
			{
				if (win->IsRelativeMouseMode())
					win->SetRelativeMouseMode(false);  // keep the cursor free for the menu
			}
			else if (!win->IsRelativeMouseMode() && GetInput()->IsPressed(InputCode::Mouse_Left))
			{
				// Re-grab after a focus-loss auto-release (or the initial focus-in).
				win->SetRelativeMouseMode(true);
			}
		}

		void SetMenuOpen(bool open)
		{
			if (open == m_MenuOpen)
				return;
			m_MenuOpen = open;
			if (open)
			{
				GetWindow()->SetRelativeMouseMode(false);  // free the cursor for the menu
			}
			else
			{
				GetWindow()->SetRelativeMouseMode(true);   // back to mouselook
				m_Settings.Save();                          // persist on close
			}
		}

		// The frame's look delta from the captured cursor, scaled by sensitivity and
		// invert-Y; (0,0) when released (menu open / unfocused).
		glm::vec2 LookDelta() const
		{
			float dx = 0.0f, dy = 0.0f;
			if (GetWindow()->IsRelativeMouseMode())
				GetWindow()->GetRelativeMouseDelta(dx, dy);
			const float s = m_Settings.mouseSensitivity;
			const float ySign = m_Settings.invertY ? 1.0f : -1.0f;  // screen-down is +y
			return glm::vec2(dx * s, dy * ySign * s);
		}

		// The pause/settings menu. Controls apply live; Video changes hit the
		// renderer/window immediately. Audio is a placeholder (no audio system yet).
		void DrawPauseMenu()
		{
			ImGuiIO& io = ImGui::GetIO();
			ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
				ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Always);

			ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
			if (ImGui::Begin("Paused", nullptr, flags))
			{
				if (ImGui::BeginTabBar("SettingsTabs"))
				{
					if (ImGui::BeginTabItem("Controls"))
					{
						ImGui::SliderFloat("Mouse Sensitivity", &m_Settings.mouseSensitivity, 0.1f, 3.0f, "%.2f");
						ImGui::Checkbox("Invert Y", &m_Settings.invertY);
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Video"))
					{
						const char* kQuality[] = { "Low (1024)", "Medium (2048)", "High (4096)" };
						if (ImGui::Combo("Shadow Quality", &m_Settings.shadowQuality, kQuality, 3))
							GetRenderer()->SetShadowResolution(GameSettings::ShadowResolutionFor(m_Settings.shadowQuality));

						bool vsync = m_Settings.vsync;
						if (ImGui::Checkbox("V-Sync", &vsync))
						{
							m_Settings.vsync = vsync;
							GetRenderer()->SetVSync(vsync);
						}

						DrawResolutionCombo();

						ImGui::TextDisabled("Fullscreen: not supported yet (windowed only).");
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Audio"))
					{
						ImGui::TextDisabled("No audio system yet.");
						ImGui::EndTabItem();
					}
					ImGui::EndTabBar();
				}

				ImGui::Separator();
				if (ImGui::Button("Resume", ImVec2(-1.0f, 0.0f)))
					SetMenuOpen(false);
				if (ImGui::Button("Quit to Desktop", ImVec2(-1.0f, 0.0f)))
				{
					m_Settings.Save();
					Quit();
				}
			}
			ImGui::End();
		}

		// Contextual interact prompt, low-center. Shows "ride" when you're on foot and
		// in range of the bike, and "dismount" while riding. Deliberately a plain
		// borderless overlay, not a window — this is diegetic HUD, not settings UI.
		void DrawInteractPrompt()
		{
			const char* label = nullptr;
			if (IsRiding())        label = "[E]  Dismount";
			else if (CanMount())   label = "[E]  Ride Hoverbike";
			ImGuiIO& io = ImGui::GetIO();

			// Fade both ways so the prompt doesn't pop as you cross the mount radius.
			// Fading OUT means drawing after the prompt is logically gone, so the last
			// label is cached — `label` is already null by then. Labels are string
			// literals, so caching the pointer is safe.
			if (label)
			{
				m_PromptLabel = label;
				m_PromptAlpha = std::min(1.0f, m_PromptAlpha + io.DeltaTime * kPromptFadeInRate);
			}
			else
			{
				m_PromptAlpha = std::max(0.0f, m_PromptAlpha - io.DeltaTime * kPromptFadeOutRate);
			}

			// Nothing to draw: never shown yet, or the fade-out has finished.
			if (!m_PromptLabel || m_PromptAlpha <= 0.0f)
				return;

			ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.82f),
				ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowBgAlpha(0.45f * m_PromptAlpha);

			ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
				| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs
				| ImGuiWindowFlags_AlwaysAutoResize;

			if (ImGui::Begin("##InteractPrompt", nullptr, flags))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, m_PromptAlpha));
				ImGui::TextUnformatted(m_PromptLabel);
				ImGui::PopStyleColor();
			}
			ImGui::End();
		}

		void DrawResolutionCombo()
		{
			struct Res { int w, h; const char* label; };
			static const Res kRes[] = {
				{ 1280, 720,  "1280 x 720"  },
				{ 1600, 900,  "1600 x 900"  },
				{ 1920, 1080, "1920 x 1080" },
				{ 2560, 1440, "2560 x 1440" },
			};
			int cur = -1;
			for (int i = 0; i < IM_ARRAYSIZE(kRes); ++i)
				if (kRes[i].w == m_Settings.width && kRes[i].h == m_Settings.height) cur = i;

			std::string preview = (cur >= 0) ? kRes[cur].label
				: (std::to_string(m_Settings.width) + " x " + std::to_string(m_Settings.height));

			if (ImGui::BeginCombo("Resolution", preview.c_str()))
			{
				for (int i = 0; i < IM_ARRAYSIZE(kRes); ++i)
				{
					if (ImGui::Selectable(kRes[i].label, i == cur))
					{
						m_Settings.width = kRes[i].w;
						m_Settings.height = kRes[i].h;
						GetWindow()->SetSize(m_Settings.width, m_Settings.height);
					}
				}
				ImGui::EndCombo();
			}
		}

		// Free-fly navigation (fallback when there's no walkable terrain).
		void UpdateFlyCam(float deltaTime)
		{
			if (GetWindow()->IsRelativeMouseMode())
			{
				m_Camera->SetRotationInput(LookDelta());

				glm::vec3 move(0.0f);
				if (GetInput()->IsDown(InputCode::Key_W)) move.z += 1.0f;
				if (GetInput()->IsDown(InputCode::Key_S)) move.z -= 1.0f;
				if (GetInput()->IsDown(InputCode::Key_D)) move.x += 1.0f;
				if (GetInput()->IsDown(InputCode::Key_A)) move.x -= 1.0f;
				if (GetInput()->IsDown(InputCode::Key_Space)) move.y += 1.0f;
				if (GetInput()->IsDown(InputCode::Key_Control)) move.y -= 1.0f;
				m_Camera->SetMovementInput(move);
				m_Camera->isSprinting = GetInput()->IsDown(InputCode::Key_Shift);
			}
			else
			{
				m_Camera->SetRotationInput(glm::vec2(0.0f));
				m_Camera->SetMovementInput(glm::vec3(0.0f));
			}
			m_Camera->Update(deltaTime);
		}

		// Build the pawn framework: an on-foot CharacterPawn and a hoverbike
		// VehiclePawn, each with a placeholder body, sharing one ground sampler and
		// one camera rig. Possession starts on foot.
		void InitCharacter()
		{
			// Ground-height query. Lazy: returns a flat floor until the terrain's CPU
			// heightfield readback completes, then real heights. Shared by both pawns
			// (foot snap / hover) and the camera rig (boom clearance).
			m_GroundFn = [this](float x, float z) -> float {
				if (m_TerrainReady && m_Terrain.HasHeightData())
					return m_Terrain.GetHeightAt(x, z);
				return 0.0f;
			};

			m_Character = std::make_unique<CharacterPawn>();
			m_Character->SetGroundSampler(m_GroundFn);

			m_Bike = std::make_unique<VehiclePawn>();
			m_Bike->SetGroundSampler(m_GroundFn);

			Possess(m_Character.get());

			// Placeholder bodies: scaled test cubes (uv_checker so facing reads while
			// turning). Swap for real meshes later.
			//
			// Scene::AddPrimitive emplaces into a vector, so an earlier SceneObject*
			// dangles once a later add reallocates it — these are stored as indices
			// and resolved per use.
			m_BodyIndex = AddPlaceholder("PlayerBody");
			m_BikeIndex = AddPlaceholder("Hoverbike");
		}

		// Adds a uv_checker unit cube primitive; returns its scene index (-1 on
		// failure, e.g. no test geometry).
		int AddPlaceholder(const char* name)
		{
			auto* vb = GetRenderer()->GetTestVertexBuffer();
			auto* ib = GetRenderer()->GetTestIndexBuffer();
			uint32_t ic = GetRenderer()->GetTestIndexCount();
			if (!vb || !ib || ic == 0)
				return -1;

			auto mesh = std::make_unique<MeshDrawable>(vb, ib, ic, PipelineType::Mesh);
			if (auto* res = GetRenderer()->GetResourceManager())
				if (auto* tex = res->GetTexture("uv_checker"))
					mesh->AddTexture(tex);
			m_Scene->AddPrimitive(name, std::move(mesh));
			return static_cast<int>(m_Scene->GetObjectCount()) - 1;
		}

		SceneObject* SceneObjectAt(int index)
		{
			if (index < 0 || index >= static_cast<int>(m_Scene->GetObjectCount()))
				return nullptr;
			return m_Scene->GetObject(static_cast<size_t>(index));
		}

		// Possession = swap which pawn receives input + drives the camera. The boom
		// length is a property of the pawn, so it re-reads on every swap.
		void Possess(Pawn* pawn)
		{
			if (m_Possessed == pawn)
				return;
			if (m_Possessed)
				m_Possessed->OnUnpossess();
			m_Possessed = pawn;
			if (m_Possessed)
			{
				m_Possessed->OnPossess();
				m_CameraRig.GetConfig().distance = m_Possessed->GetCameraDistance();
			}
		}

		bool IsRiding() const { return m_Possessed == m_Bike.get(); }

		// On foot and close enough to the bike to mount it. Drives both the E
		// handler and the on-screen prompt, so they can't disagree about range.
		bool CanMount() const
		{
			if (IsRiding() || !m_CharacterStarted)
				return false;
			const glm::vec3 toBike = m_Bike->GetPosition() - m_Character->GetPosition();
			return glm::dot(toBike, toBike) <= kMountRadius * kMountRadius;
		}

		// Third-person control: feed the look delta to the camera rig, build a
		// device-neutral PawnInput, step whichever pawn is possessed, then orbit the
		// camera around it and sync the placeholder bodies.
		void UpdateCharacterMode(float deltaTime)
		{
			const bool controlling = GetWindow()->IsRelativeMouseMode();

			// Place the pawns on first entry (handles the fly->walk switch once the
			// terrain readback lands), mirroring the old lazy init.
			if (!m_CharacterStarted)
			{
				glm::vec3 camPos = m_Camera->GetPosition();
				m_Character->SetPosition(glm::vec3(camPos.x, m_GroundFn(camPos.x, camPos.z), camPos.z));
				m_Character->SetYaw(m_Camera->GetYaw());
				m_CameraRig.SetYawPitch(m_Camera->GetYaw(), m_Camera->GetPitch());

				// Park the bike a few metres ahead so it's findable on spawn.
				glm::vec3 bikePos = m_Character->GetPosition() + m_Character->GetForward() * 6.0f;
				bikePos.y = m_GroundFn(bikePos.x, bikePos.z) + m_Bike->GetConfig().hoverHeight;
				m_Bike->SetPosition(bikePos);
				m_Bike->SetYaw(m_Character->GetYaw());

				m_CharacterStarted = true;
			}

			m_CameraRig.ApplyLook(controlling ? LookDelta() : glm::vec2(0.0f));

			// One input struct, all intents filled; each pawn reads what it cares
			// about (CharacterPawn ignores throttle/steer, VehiclePawn ignores move).
			PawnInput in;
			in.cameraForward = m_CameraRig.GetFlatForward();
			in.cameraRight   = m_CameraRig.GetFlatRight();
			if (controlling)
			{
				if (GetInput()->IsDown(InputCode::Key_W)) in.move.y += 1.0f;
				if (GetInput()->IsDown(InputCode::Key_S)) in.move.y -= 1.0f;
				if (GetInput()->IsDown(InputCode::Key_D)) in.move.x += 1.0f;
				if (GetInput()->IsDown(InputCode::Key_A)) in.move.x -= 1.0f;
				in.sprint      = GetInput()->IsDown(InputCode::Key_Shift);
				in.jumpPressed = GetInput()->IsPressed(InputCode::Key_Space);

				in.throttle        = GetInput()->IsDown(InputCode::Key_W) ? 1.0f : 0.0f;
				in.brake           = GetInput()->IsDown(InputCode::Key_S) ? 1.0f : 0.0f;
				in.steer           = in.move.x;
				in.interactPressed = GetInput()->IsPressed(InputCode::Key_E);
			}

			if (in.interactPressed)
				ToggleRide();

			m_Possessed->Update(in, deltaTime);

			// The bike keeps hovering (and settling onto the terrain) while parked, so
			// it doesn't sink or hang in the air after you step off.
			if (!IsRiding())
				m_Bike->Update(PawnInput{}, deltaTime);

			m_CameraRig.UpdateCamera(*m_Camera, m_Possessed->GetCameraFocus(), &m_GroundFn);
			SyncBodies();
		}

		// Mount if on foot and close enough to the bike; dismount if riding.
		void ToggleRide()
		{
			if (IsRiding())
			{
				// Step off to the bike's right, snapped to the ground.
				m_Character->SetPosition(m_Bike->GetDismountPosition(1.0f));
				m_Character->SetYaw(m_Bike->GetYaw());
				Possess(m_Character.get());
				return;
			}

			if (CanMount())
				Possess(m_Bike.get());
		}

		// Drive the placeholder primitives from the pawns. While riding, the rider
		// box is parked on the bike's seat (so mounting visibly reads) — it follows
		// the bike's full transform, bank and all.
		void SyncBodies()
		{
			// Bike: a long, low box. Front is +X to match the pawn yaw convention.
			if (SceneObject* bikeObj = SceneObjectAt(m_BikeIndex))
			{
				if (bikeObj->meshDrawable)
				{
					const glm::vec3 bikeScale(2.2f, 0.5f, 0.7f);
					bikeObj->meshDrawable->SetTransform(
						m_Bike->GetVisualTransform() * glm::scale(glm::mat4(1.0f), bikeScale));
				}
			}

			// Rider: person-sized box, feet on the ground when walking, seated when riding.
			if (SceneObject* bodyObj = SceneObjectAt(m_BodyIndex))
			{
				if (bodyObj->meshDrawable)
				{
					const glm::vec3 bodyScale(0.6f, 1.8f, 0.4f);
					const float halfHeight = 0.9f;  // half the 1.8 m box

					glm::mat4 base = IsRiding()
						? m_Bike->GetVisualTransform()
							* glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, m_Bike->GetConfig().seatHeight, 0.0f))
						: m_Character->GetVisualTransform();

					bodyObj->meshDrawable->SetTransform(
						base
						* glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, halfHeight, 0.0f))
						* glm::scale(glm::mat4(1.0f), bodyScale));
				}
			}
		}

		float AspectRatio() const
		{
			uint32_t w = GetRenderer()->GetWidth();
			uint32_t h = GetRenderer()->GetHeight();
			return (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : (1280.0f / 720.0f);
		}

		// Directory the executable lives in (robust for a double-clicked packaged
		// build). Uses argv[0] made absolute against the CWD; falls back to the CWD.
		std::filesystem::path ExecutableDir() const
		{
#ifdef _WIN32
			if (__argc > 0 && __argv[0] && __argv[0][0] != '\0')
			{
				std::error_code ec;
				std::filesystem::path exe = std::filesystem::absolute(__argv[0], ec);
				if (!ec) return exe.parent_path();
			}
#endif
			return std::filesystem::current_path();
		}

		// Project resolution, in priority order:
		//   1. An explicit .nbproj path as the first CLI arg.
		//   2. Packaged layout: a "Project/" folder next to the exe holding one .nbproj
		//      (this is what the M3 packager produces — see PACKAGING_ROADMAP.md).
		//   3. Dev fallback: the bundled Sample under <repoRoot>/Projects (repo markers).
		std::filesystem::path ResolveProjectFile() const
		{
#ifdef _WIN32
			if (__argc > 1 && __argv[1] && __argv[1][0] != '\0')
				return std::filesystem::path(__argv[1]);
#endif
			std::error_code ec;
			std::filesystem::path bundled = ExecutableDir() / "Project";
			if (std::filesystem::is_directory(bundled, ec))
			{
				for (const auto& e : std::filesystem::directory_iterator(bundled, ec))
				{
					if (e.path().extension() == ".nbproj")
						return e.path();
				}
			}

			std::filesystem::path search = std::filesystem::current_path();
			while (true)
			{
				if (std::filesystem::exists(search / ".git") &&
					std::filesystem::exists(search / "Setup.bat"))
					return search / "Projects" / "Sample" / "Sample.nbproj";
				if (!search.has_parent_path() || search == search.parent_path())
					break;
				search = search.parent_path();
			}
			return {};
		}

		// Hardcoded terrain for the runtime (mirrors the editor's TerrainPanel defaults).
		// M2b will generate a CPU heightfield from this for the character controller;
		// eventually the terrain params come from the serialized scene, not code.
		void InitTerrain()
		{
			if (!m_Terrain.Initialize(GetRenderer()))
			{
				LOG_ERROR("Game: terrain init failed");
				return;
			}

			TerrainDesc desc;
			desc.resolution = kTerrainResolution;
			desc.worldSize  = 200.0f;
			desc.heightScale = 30.0f;
			desc.position = glm::vec3(0.0f);
			desc.noise.width = 256;
			desc.noise.height = 256;
			desc.noise.depth = 1;
			desc.noise.noiseType = NoiseType::Perlin;
			desc.noise.octaves = 6;
			desc.noise.frequency = 3.0f;
			desc.noise.persistence = 0.5f;
			desc.noise.lacunarity = 2.0f;
			desc.noise.seed = 42;
			desc.noise.debugName = "TerrainHeightmap";

			m_Terrain.Regenerate(desc);
			m_TerrainReady = true;
			LOG_INFO("Game: terrain ready");
		}

		void LoadStartScene()
		{
			std::filesystem::path scenePath = m_Project.StartScenePath();
			if (!std::filesystem::exists(scenePath))
			{
				LOG_ERROR("Game: start scene missing: {}", scenePath.string());
				return;
			}

			GetRenderer()->WaitForIdle();

			SceneCameraState cam;
			std::string editorBlob;  // ignored: day/night etc. is editor-side (see M2b notes)
			if (SceneSerializer::Load(*m_Scene, cam, scenePath.string(), GetRenderer(), &editorBlob))
			{
				m_Camera->SetPosition(cam.position);
				m_Camera->SetRotation(cam.yaw, cam.pitch);
				m_Camera->SetPerspectiveInfiniteReverseZ(cam.fov, AspectRatio(), cam.nearPlane);
				if (m_Scene->GetLightCount() > 0)
					GetRenderer()->SetShadowConfig(m_Scene->GetLight(0)->shadowConfig);
				LOG_INFO("Game: loaded start scene '{}'", scenePath.string());
			}
		}

		static constexpr uint32_t kTerrainResolution = 256;

		std::unique_ptr<Scene>  m_Scene;
		std::unique_ptr<Camera> m_Camera;
		Project                 m_Project;
		TerrainSystem           m_Terrain;
		bool                    m_TerrainReady = false;

		GameSettings            m_Settings;         // persisted video/controls settings
		bool                    m_MenuOpen = false; // pause/settings menu shown

		// Pawn framework. Possession is a simple Pawn* here; CharacterPawn owns the
		// kinematic walking, VehiclePawn the hoverbike, the rig the camera.
		std::unique_ptr<CharacterPawn>    m_Character;
		std::unique_ptr<VehiclePawn>      m_Bike;
		Pawn*                             m_Possessed = nullptr;
		ThirdPersonCameraRig              m_CameraRig;
		std::function<float(float, float)> m_GroundFn;             // terrain height query
		bool                              m_CharacterStarted = false;

		// Placeholder bodies, held as scene *indices* — Scene::AddPrimitive emplaces
		// into a vector, so a stored SceneObject* would dangle on the next add.
		int m_BodyIndex = -1;
		int m_BikeIndex = -1;

		// How close you must be, on foot, to mount the bike.
		static constexpr float kMountRadius = 3.5f;

		// Interact-prompt fade. Out is slower than in: appearing should feel
		// responsive, disappearing shouldn't yank your eye back to it.
		static constexpr float kPromptFadeInRate  = 6.0f;   // alpha/sec
		static constexpr float kPromptFadeOutRate = 3.5f;   // alpha/sec

		float       m_PromptAlpha = 0.0f;
		const char* m_PromptLabel = nullptr;  // last shown label, kept alive for the fade-out
	};

	Application* CreateApplication()
	{
		return new GameApplication();
	}
}
