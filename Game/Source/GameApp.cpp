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

#include <filesystem>
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

			// Walk on the terrain once it has CPU height data; otherwise fly.
			if (m_TerrainReady && m_Terrain.HasHeightData())
				UpdateCharacter(deltaTime);
			else
				UpdateFlyCam(deltaTime);

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

		// Kinematic character: WASD walks in the look plane, gravity + snap to the
		// terrain surface (via TerrainSystem::GetHeightAt), Space jumps. The camera is
		// driven from the body's eye position; the Camera class is used for orientation
		// only. Right-mouse to look for now — cursor-lock for always-on mouselook is the
		// obvious feel follow-up (needs a window cursor-capture API).
		void UpdateCharacter(float deltaTime)
		{
			if (!m_CharInitialized)
			{
				glm::vec3 p = m_Camera->GetPosition();
				m_CharPos = glm::vec3(p.x, m_Terrain.GetHeightAt(p.x, p.z), p.z);
				m_CharVelY = 0.0f;
				m_CharInitialized = true;
			}

			m_Camera->SetRotationInput(LookDelta());       // always-on mouselook when captured
			m_Camera->SetMovementInput(glm::vec3(0.0f));   // body movement handled below
			m_Camera->Update(deltaTime);

			const bool controlling = GetWindow()->IsRelativeMouseMode();

			// Horizontal wish-direction in the look-yaw plane (ignore pitch).
			glm::vec3 fwd = m_Camera->GetForward();  fwd.y = 0.0f;
			glm::vec3 right = m_Camera->GetRight();  right.y = 0.0f;
			if (glm::dot(fwd, fwd) > 1e-5f)     fwd = glm::normalize(fwd);
			if (glm::dot(right, right) > 1e-5f) right = glm::normalize(right);

			glm::vec3 wish(0.0f);
			if (controlling)
			{
				if (GetInput()->IsDown(InputCode::Key_W)) wish += fwd;
				if (GetInput()->IsDown(InputCode::Key_S)) wish -= fwd;
				if (GetInput()->IsDown(InputCode::Key_D)) wish += right;
				if (GetInput()->IsDown(InputCode::Key_A)) wish -= right;
			}
			if (glm::dot(wish, wish) > 1e-5f) wish = glm::normalize(wish);

			float speed = kWalkSpeed * (GetInput()->IsDown(InputCode::Key_Shift) ? kSprintMul : 1.0f);
			m_CharPos.x += wish.x * speed * deltaTime;
			m_CharPos.z += wish.z * speed * deltaTime;

			// Gravity, then snap to the ground.
			m_CharVelY += kGravity * deltaTime;
			m_CharPos.y += m_CharVelY * deltaTime;

			float groundY = m_Terrain.GetHeightAt(m_CharPos.x, m_CharPos.z);
			bool grounded = false;
			if (m_CharPos.y <= groundY)
			{
				m_CharPos.y = groundY;
				m_CharVelY = 0.0f;
				grounded = true;
			}
			if (grounded && controlling && GetInput()->IsPressed(InputCode::Key_Space))
				m_CharVelY = kJumpSpeed;

			m_Camera->SetPosition(m_CharPos + glm::vec3(0.0f, kEyeHeight, 0.0f));
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

		// Kinematic character tunables (feel — expect to tweak these live).
		static constexpr float kEyeHeight = 1.7f;    // camera height above the feet
		static constexpr float kWalkSpeed = 6.0f;    // m/s
		static constexpr float kSprintMul = 2.0f;
		static constexpr float kGravity   = -20.0f;  // m/s^2 (snappier than 9.8)
		static constexpr float kJumpSpeed = 8.0f;    // m/s initial jump velocity

		std::unique_ptr<Scene>  m_Scene;
		std::unique_ptr<Camera> m_Camera;
		Project                 m_Project;
		TerrainSystem           m_Terrain;
		bool                    m_TerrainReady = false;

		GameSettings            m_Settings;         // persisted video/controls settings
		bool                    m_MenuOpen = false; // pause/settings menu shown

		glm::vec3 m_CharPos = glm::vec3(0.0f);  // feet position
		float     m_CharVelY = 0.0f;
		bool      m_CharInitialized = false;
	};

	Application* CreateApplication()
	{
		return new GameApplication();
	}
}
