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
#include "Engine/Renderer/Components/GpuProfiler.hpp"
#include "Engine/Gameplay/Pawn.hpp"
#include "Engine/Gameplay/CharacterPawn.hpp"
#include "Engine/Gameplay/VehiclePawn.hpp"
#include "Engine/Gameplay/ThirdPersonCameraRig.hpp"
#include "Engine/Gameplay/GuardAgent.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <filesystem>
#include <chrono>
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

			// Force a night mission look. The saved Sample scene is lit for
			// daytime, which flattens all the stealth lighting — braziers only
			// read as light against darkness. This overrides the loaded scene's
			// ambient + moon so the camp actually feels like night.
			SetupNightLighting();

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
			// The vehicle is camp-only — it's just a distraction in the range.
			if (SceneObject* bike = SceneObjectAt(m_BikeIndex))
				bike->visible = characterMode && (m_Level == Level::Camp);

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
			{
				DrawPauseMenu();
			}
			else
			{
				DrawInteractPrompt();
				if (m_CharacterStarted)
				{
					DrawDetectionHUD();
					DrawGuardTracking();
					DrawThrowPreview();
					DrawRangeOverlay();
					DrawTestRangePanel();
				}
			}
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

			// Tab frees the cursor to drag the test-range sliders WITHOUT pausing
			// — the game keeps running so the guard's meter reacts live while you
			// tune. Only meaningful in the range; ignored in the camp.
			if (!m_MenuOpen && m_Level == Level::TestRange
				&& GetInput()->IsPressed(InputCode::Key_Tab))
			{
				m_DebugCursorFree = !m_DebugCursorFree;
				win->SetRelativeMouseMode(!m_DebugCursorFree);
			}

			if (m_MenuOpen)
			{
				if (win->IsRelativeMouseMode())
					win->SetRelativeMouseMode(false);  // keep the cursor free for the menu
			}
			else if (m_DebugCursorFree)
			{
				// Hold the cursor free for the debug panel — do NOT re-grab on a
				// click, or dragging a slider would recapture and hide it.
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
				m_DebugCursorFree = false;                 // the menu takes the cursor
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
				// Level switch. Parked in the pause menu for now — it's a dev
				// affordance, not a shipping menu item.
				ImGui::TextDisabled("Level");
				const bool inRange = (m_Level == Level::TestRange);
				if (ImGui::Button(inRange ? "Go to Camp" : "Go to Test Range", ImVec2(200.0f, 0.0f)))
				{
					SwitchLevel(inRange ? Level::Camp : Level::TestRange);
					SetMenuOpen(false);
				}
				ImGui::SameLine();
				ImGui::TextDisabled(inRange ? "(test range)" : "(camp)");
				ImGui::Separator();

				if (ImGui::BeginTabBar("SettingsTabs"))
				{
					if (ImGui::BeginTabItem("Controls"))
					{
						ImGui::SliderFloat("Mouse Sensitivity", &m_Settings.mouseSensitivity, 0.1f, 3.0f, "%.2f");
						ImGui::Checkbox("Invert Y", &m_Settings.invertY);

						// Not rebindable yet, but undiscoverable otherwise —
						// there's no tutorial and crouch has no other affordance.
						ImGui::Separator();
						ImGui::TextDisabled("Bindings");
						static const struct { const char* key; const char* action; } kBindings[] = {
							{ "W A S D",    "Move"              },
							{ "Mouse",      "Look"              },
							{ "Shift",      "Sprint"            },
							{ "Ctrl (tap)", "Toggle crouch"     },
						{ "Ctrl (hold)","Go prone / crawl"  },
							{ "Space",      "Jump / climb ledge" },
							{ "Q (hold)",   "Aim stone, release to throw" },
							{ "F (hold)",   "Survey guards" },
							{ "E",          "Interact / takedown" },
							{ "R",          "Restart mission"   },
							{ "Esc",        "This menu"         },
						};
						if (ImGui::BeginTable("##bindings", 2, ImGuiTableFlags_SizingFixedFit))
						{
							for (const auto& b : kBindings)
							{
								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(b.key);
								ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("%s", b.action);
							}
							ImGui::EndTable();
						}
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
			// Same priority order as Interact(), so the prompt can never promise
			// something different from what the key will actually do.
			const char* label = nullptr;
			if (MissionOver())                     label = nullptr;
			else if (FindTakedownTarget() >= 0)    label = "[E]  Takedown";
			else if (CanTakeRelic())               label = "[E]  Take the relic";
			else if (IsRiding())                   label = "[E]  Dismount";
			else if (CanMount())                   label = "[E]  Ride Hoverbike";
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

		// Detection readout: state, meter, and whether the guard has eyes on you
		// right now. Scaffolding for tuning — a shipped game would sell all of
		// this diegetically (guard animation, audio sting, a screen-edge vignette)
		// rather than with a debug bar. But you cannot tune what you can't see,
		// and the SEEN/hidden line in particular is what tells you whether a
		// broken sight line was really line-of-sight or just the cone angle.
		void DrawDetectionHUD()
		{
			ImGuiIO& io = ImGui::GetIO();
			ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.06f),
				ImGuiCond_Always, ImVec2(0.5f, 0.0f));
			ImGui::SetNextWindowBgAlpha(0.35f);

			ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
				| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs
				| ImGuiWindowFlags_AlwaysAutoResize;

			if (ImGui::Begin("##DetectionHUD", nullptr, flags))
			{
				// Report the WORST guard, not an average: the one closest to
				// spotting you is the only one that matters, and averaging would
				// hide a guard about to catch you behind two who can't see you.
				const GuardSlot* worst = nullptr;
				bool anySight = false;
				int  aliveCount = 0;
				for (const auto& slot : m_Guards)
				{
					if (!slot.alive) continue;
					++aliveCount;
					anySight = anySight || slot.agent.CanSeeTarget();
					if (!worst || slot.agent.GetDetection() > worst->agent.GetDetection())
						worst = &slot;
				}

				// --- Objective ----------------------------------------------
				switch (m_Mission)
				{
				case Mission::Infiltrate:
					ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.9f, 1.0f), "Steal the relic");
					break;
				case Mission::Extract:
					ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "Get out with the relic");
					break;
				case Mission::Escaped:
					ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.5f, 1.0f), "ESCAPED  -  [R] again");
					break;
				case Mission::Caught:
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.25f, 1.0f), "CAUGHT  -  [R] retry");
					break;
				}

				// --- Detection ----------------------------------------------
				if (!MissionOver() && worst)
				{
					const char* stateText = "UNAWARE";
					ImVec4      stateColor(0.45f, 0.9f, 0.5f, 1.0f);
					switch (worst->agent.GetAlertState())
					{
					case AlertState::Suspicious:
						stateText = "SUSPICIOUS"; stateColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); break;
					case AlertState::Alerted:
						stateText = "SPOTTED";    stateColor = ImVec4(1.0f, 0.3f, 0.25f, 1.0f); break;
					case AlertState::Unaware:
					default: break;
					}

					// Investigation is its own word: "suspicious" reads as
					// "someone saw you", which is misleading when they're
					// actually walking toward a noise you made on purpose.
					int investigating = 0;
					for (const auto& s : m_Guards)
						if (s.alive && s.agent.IsInvestigating()) ++investigating;

					if (investigating > 0 && worst->agent.GetAlertState() != AlertState::Alerted)
					{
						stateText = "SEARCHING";
						stateColor = ImVec4(1.0f, 0.65f, 0.15f, 1.0f);
					}

					ImGui::TextColored(stateColor, "%s", stateText);
					ImGui::SameLine();
					ImGui::TextDisabled(anySight ? "(in sight)" : "(hidden)");

					// Exposure readout. The player has to be able to READ how
					// hidden they are, or shadow is an invisible dice roll.
					const char* expText; ImVec4 expColor;
					if (m_PlayerExposure > 0.7f)      { expText = "LIT";    expColor = ImVec4(1.0f, 0.75f, 0.3f, 1.0f); }
					else if (m_PlayerExposure > 0.35f){ expText = "DIM";    expColor = ImVec4(0.8f, 0.8f, 0.55f, 1.0f); }
					else                              { expText = "SHADOW"; expColor = ImVec4(0.45f, 0.6f, 0.85f, 1.0f); }
					ImGui::SameLine();
					ImGui::TextColored(expColor, "| %s", expText);

					// Noise readout — you can't manage a channel you can't see.
					if (m_PlayerNoise > 0.05f)
					{
						const char* nText; ImVec4 nColor;
						if (m_PlayerNoise > 0.6f)      { nText = "LOUD";  nColor = ImVec4(1.0f, 0.45f, 0.35f, 1.0f); }
						else if (m_PlayerNoise > 0.3f) { nText = "NOISY"; nColor = ImVec4(1.0f, 0.8f, 0.4f, 1.0f); }
						else                           { nText = "quiet"; nColor = ImVec4(0.6f, 0.7f, 0.6f, 1.0f); }
						ImGui::SameLine();
						ImGui::TextColored(nColor, "| %s", nText);
					}

					// Cover readout: grass only pays off when you're low, so
					// nudge the player toward crawling when they're standing in it.
					if (m_PlayerConcealment < 0.9f)
					{
						ImGui::SameLine();
						if (m_PlayerConcealment < 0.2f)
							ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "| HIDDEN");
						else
							ImGui::TextColored(ImVec4(0.7f, 0.8f, 0.5f, 1.0f), "| in grass (crawl!)");
					}

					glm::vec3 tint = worst->agent.GetDebugTint();
					ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(tint.r, tint.g, tint.b, 1.0f));
					ImGui::ProgressBar(worst->agent.GetDetection(), ImVec2(220.0f, 12.0f), "");
					ImGui::PopStyleColor();

					// Resources and evidence. Bodies found is the one that
					// should make you wince — it's the record of your mistakes.
					int bodiesFound = 0;
					for (const auto& s : m_Guards)
						if (!s.alive && s.bodyFound) ++bodiesFound;

					ImGui::TextDisabled("guards %d/%d   stones %d   searching %d",
						aliveCount, kGuardCount, m_StonesLeft, investigating);
					if (bodiesFound > 0)
						ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.3f, 1.0f),
							"bodies discovered: %d", bodiesFound);
				}

				// Perception cost vs. total frame cost. If perception is the
				// frame-rate culprit these two track each other; if the frame
				// spikes while perception stays flat, the cause is elsewhere.
				ImGuiIO& hudIo = ImGui::GetIO();
				float frameMs = hudIo.DeltaTime * 1000.0f;
				m_WorstFrameMs = std::max(m_WorstFrameMs, frameMs);
				float avgGuardMs = (m_GuardSamples > 0) ? (m_GuardMsAccum / m_GuardSamples) : 0.0f;

				ImGui::Separator();
				ImGui::TextDisabled("frame %5.2f ms (worst %5.2f)", frameMs, m_WorstFrameMs);
				ImGui::TextDisabled("guard %5.3f ms (peak %5.3f)", avgGuardMs, m_GuardMsPeak);
				ImGui::TextDisabled("objects %d", (int)m_Scene->GetObjectCount());
			}
			ImGui::End();
		}

		//----------------------------------------------------------------------
		// Screen-space overlays
		//----------------------------------------------------------------------

		// World -> screen. Returns false if the point is behind the camera or
		// off-screen, so callers can just skip it.
		//
		// NOTE the engine uses an INFINITE REVERSE-Z projection: near = 1,
		// far = 0, and there is no far plane. So the behind-camera test is
		// clip.w <= 0, not a z-range check — comparing against depth here would
		// silently reject everything.
		bool WorldToScreen(const glm::vec3& world, ImVec2& outScreen) const
		{
			const glm::mat4 viewProj = m_Camera->GetProjectionMatrix() * m_Camera->GetViewMatrix();
			glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
			if (clip.w <= 1e-4f) return false;

			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			if (ndc.x < -1.3f || ndc.x > 1.3f || ndc.y < -1.3f || ndc.y > 1.3f) return false;

			ImGuiIO& io = ImGui::GetIO();
			outScreen = ImVec2((ndc.x * 0.5f + 0.5f) * io.DisplaySize.x,
				(ndc.y * 0.5f + 0.5f) * io.DisplaySize.y);
			return true;
		}

		glm::mat4 ViewProj() const
		{
			return m_Camera->GetProjectionMatrix() * m_Camera->GetViewMatrix();
		}

		ImVec2 ClipToScreen(const glm::vec4& clip) const
		{
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			ImGuiIO& io = ImGui::GetIO();
			return ImVec2((ndc.x * 0.5f + 0.5f) * io.DisplaySize.x,
				(ndc.y * 0.5f + 0.5f) * io.DisplaySize.y);
		}

		// Draw a WORLD-space line segment, clipped at the near plane so it never
		// just vanishes when one end is behind the camera — the reason the range
		// rings were disappearing when you stood close to the guard. A segment
		// straddling the camera plane is trimmed to the plane instead of dropped.
		void DrawWorldLine(const glm::vec3& a, const glm::vec3& b, ImU32 col, float thick) const
		{
			const glm::mat4 vp = ViewProj();
			glm::vec4 ca = vp * glm::vec4(a, 1.0f);
			glm::vec4 cb = vp * glm::vec4(b, 1.0f);

			const float wMin = 1e-3f;
			const bool ai = ca.w > wMin, bi = cb.w > wMin;
			if (!ai && !bi) return;                    // wholly behind the camera
			if (ai != bi)                              // straddles: trim to the plane
			{
				const float t = (wMin - ca.w) / (cb.w - ca.w);
				const glm::vec4 mid = ca + t * (cb - ca);
				if (ai) cb = mid; else ca = mid;
			}
			ImGui::GetBackgroundDrawList()->AddLine(ClipToScreen(ca), ClipToScreen(cb), col, thick);
		}

		static ImU32 AlertColor(const GuardAgent& g, float alpha)
		{
			const glm::vec3 t = g.GetDebugTint();
			return ImGui::GetColorU32(ImVec4(t.r, t.g, t.b, alpha));
		}

		// Guard tracking. Stealth is a game of information: if you can't keep
		// track of where the patrols are, you aren't planning, you're guessing,
		// and guessing isn't tense — it's just opaque.
		//
		// Held (not toggled) and it stops you moving usefully while you read it,
		// so surveying is an action with a cost rather than a permanent overlay
		// that flattens the whole camp into a map.
		void DrawGuardTracking()
		{
			if (!m_TrackingHeld) return;

			ImDrawList* dl = ImGui::GetBackgroundDrawList();
			const glm::vec3 playerPos = m_Character->GetPosition();

			for (const auto& slot : m_Guards)
			{
				const glm::vec3 anchor = slot.alive
					? slot.agent.GetPosition() + glm::vec3(0.0f, 2.1f, 0.0f)
					: slot.bodyPosition + glm::vec3(0.0f, 0.5f, 0.0f);

				ImVec2 screen;
				if (!WorldToScreen(anchor, screen)) continue;

				const float distance = glm::length(anchor - playerPos);

				if (!slot.alive)
				{
					// Bodies are tracked too — knowing which of your mistakes is
					// lying where is exactly the information you want.
					const ImU32 col = ImGui::GetColorU32(ImVec4(0.6f, 0.15f, 0.15f, 0.85f));
					dl->AddLine(ImVec2(screen.x - 6, screen.y - 6), ImVec2(screen.x + 6, screen.y + 6), col, 2.0f);
					dl->AddLine(ImVec2(screen.x - 6, screen.y + 6), ImVec2(screen.x + 6, screen.y - 6), col, 2.0f);
					continue;
				}

				const ImU32 col = AlertColor(slot.agent, 0.9f);

				// Diamond marker, sized by distance so depth reads at a glance.
				const float r = std::clamp(90.0f / std::max(distance, 1.0f), 5.0f, 14.0f);
				dl->AddQuadFilled(
					ImVec2(screen.x, screen.y - r), ImVec2(screen.x + r, screen.y),
					ImVec2(screen.x, screen.y + r), ImVec2(screen.x - r, screen.y), col);

				// A stub pointing where they're facing — the single most useful
				// thing to know about a guard you're planning to walk behind.
				const glm::vec3 fwd = slot.agent.GetForward();
				ImVec2 facingEnd;
				if (WorldToScreen(anchor + fwd * 2.0f, facingEnd))
					dl->AddLine(screen, facingEnd, col, 2.0f);

				// A guard who can see you right now is the one that matters.
				if (slot.agent.CanSeeTarget())
					dl->AddCircle(screen, r + 6.0f,
						ImGui::GetColorU32(ImVec4(1.0f, 0.25f, 0.2f, 0.95f)), 0, 2.5f);

				char label[32];
				std::snprintf(label, sizeof(label), "%.0fm", distance);
				dl->AddText(ImVec2(screen.x + r + 4, screen.y - 7),
					ImGui::GetColorU32(ImVec4(1, 1, 1, 0.75f)), label);
			}
		}

		// Throw preview: the arc, where it lands, and WHO it will pull.
		// Without this a stone is a guess, and a guess can't be a plan.
		void DrawThrowPreview()
		{
			if (!m_AimingStone || m_StonesLeft <= 0 || MissionOver()) return;

			ImDrawList* dl = ImGui::GetBackgroundDrawList();

			glm::vec3 pos, vel;
			if (!ComputeThrow(pos, vel)) return;

			// Integrate the same arc the real stone will fly, so the preview
			// can't disagree with the throw.
			const float step = 1.0f / 60.0f;
			glm::vec3 landing = pos;
			bool landed = false;

			ImVec2 prev;
			bool   havePrev = false;

			for (int i = 0; i < 240 && !landed; ++i)
			{
				vel.y += kStoneGravity * step;
				pos += vel * step;

				if (pos.y <= m_GroundFn(pos.x, pos.z))
				{
					pos.y = m_GroundFn(pos.x, pos.z);
					landing = pos;
					landed = true;
				}

				ImVec2 screen;
				if (WorldToScreen(pos, screen))
				{
					if (havePrev && (i % 2) == 0)
						dl->AddLine(prev, screen, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.5f)), 1.5f);
					prev = screen;
					havePrev = true;
				}
			}

			if (!landed) return;

			// Landing marker + the noise radius, drawn as a world-space ring so
			// it sits on the ground instead of floating as a flat screen circle.
			const int kSegments = 32;
			ImVec2 first, previous;
			bool   haveFirst = false, havePrevRing = false;

			for (int i = 0; i <= kSegments; ++i)
			{
				const float a = (static_cast<float>(i) / kSegments) * 6.2831853f;
				glm::vec3 p = landing + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * kStoneNoiseRadius;
				p.y = m_GroundFn(p.x, p.z) + 0.1f;

				ImVec2 screen;
				if (!WorldToScreen(p, screen)) { havePrevRing = false; continue; }

				if (havePrevRing)
					dl->AddLine(previous, screen, ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.3f, 0.5f)), 2.0f);
				if (!haveFirst) { first = screen; haveFirst = true; }
				previous = screen;
				havePrevRing = true;
			}

			ImVec2 landScreen;
			if (WorldToScreen(landing + glm::vec3(0.0f, 0.15f, 0.0f), landScreen))
				dl->AddCircleFilled(landScreen, 5.0f,
					ImGui::GetColorU32(ImVec4(1.0f, 0.85f, 0.3f, 0.9f)));

			// Who this actually pulls. The whole point of the preview: a throw
			// you can't predict the effect of is a coin flip, not a decision.
			int affected = 0;
			for (const auto& slot : m_Guards)
			{
				if (!slot.alive) continue;
				glm::vec3 d = slot.agent.GetPosition() - landing;
				d.y = 0.0f;
				if (glm::dot(d, d) > kStoneNoiseRadius * kStoneNoiseRadius) continue;

				++affected;
				ImVec2 gs;
				if (!WorldToScreen(slot.agent.GetPosition() + glm::vec3(0.0f, 1.0f, 0.0f), gs)) continue;

				dl->AddCircle(gs, 16.0f, ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.3f, 0.95f)), 0, 3.0f);
				if (WorldToScreen(landing + glm::vec3(0.0f, 0.3f, 0.0f), landScreen))
					dl->AddLine(landScreen, gs, ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.3f, 0.35f)), 1.5f);
			}

			if (haveFirst)
			{
				char label[48];
				std::snprintf(label, sizeof(label), "will draw %d", affected);
				dl->AddText(ImVec2(landScreen.x + 10, landScreen.y - 8),
					ImGui::GetColorU32(ImVec4(1.0f, 0.85f, 0.35f, 0.95f)), label);
			}
		}

		// Draws an arc on the GROUND (following terrain height) by projecting
		// sampled points. Full circle by default. Used for range rings and the
		// vision cone — seeing the numbers as shapes in the world is what makes
		// them tunable rather than abstract.
		void DrawGroundArc(const glm::vec3& center, float radius, ImU32 col,
			float thickness = 2.0f, float a0 = 0.0f, float a1 = 6.2831853f) const
		{
			if (radius <= 0.05f) return;

			const int segments = std::clamp(static_cast<int>(radius * 4.0f), 24, 128);

			glm::vec3 prev;
			for (int i = 0; i <= segments; ++i)
			{
				const float t = static_cast<float>(i) / segments;
				const float a = glm::mix(a0, a1, t);

				glm::vec3 p = center + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * radius;
				p.y = m_GroundFn(p.x, p.z) + 0.08f;

				// Per-segment world-line clipping — no more vanishing arcs.
				if (i > 0) DrawWorldLine(prev, p, col, thickness);
				prev = p;
			}
		}

		// A ring + its vision-cone wedge (two edge lines from the guard + the arc
		// between them) at `radius`, oriented on the guard's facing.
		void DrawSightZone(const glm::vec3& gpos, float yawRad, float halfAngleRad,
			float radius, ImU32 col, float thickness) const
		{
			DrawGroundArc(gpos, radius, col, thickness, yawRad - halfAngleRad, yawRad + halfAngleRad);

			glm::vec3 base = gpos; base.y += 0.08f;
			for (float edge : { yawRad - halfAngleRad, yawRad + halfAngleRad })
			{
				glm::vec3 tip = gpos + glm::vec3(std::cos(edge), 0.0f, std::sin(edge)) * radius;
				tip.y = m_GroundFn(tip.x, tip.z) + 0.08f;
				DrawWorldLine(base, tip, col, thickness);
			}
		}

		// Effective sight range the guard would have on the player IF the player
		// were at `stanceValue` (0/1/2), given the current light + cover at the
		// player's position. Grass concealment is stance-dependent too, so it's
		// re-queried per stance.
		float EffectiveSightForStance(const GuardAgent& g, float stanceValue) const
		{
			const float stanceRange = StanceMix(stanceValue, 1.0f, kCrouchRangeMul, kProneRangeMul);
			const float shadowRange = glm::mix(kShadowRangeFloor, 1.0f, m_PlayerExposure);
			const float conceal      = ConcealmentAt(m_Character->GetPosition(), stanceValue);
			return std::max(g.Vision().rangeMeters * stanceRange * shadowRange * conceal,
				g.Vision().minAwarenessRange);
		}

		// The guard's facing, cone, and every perception radius, drawn in world
		// space. Answers "where is it looking and how far can it actually see
		// me right now" at a glance.
		void DrawRangeOverlay()
		{
			if (m_Level != Level::TestRange || !m_ShowRangeOverlay || m_Guards.empty()) return;

			const GuardSlot& g = m_Guards[0];
			if (!g.alive) return;

			const glm::vec3 gpos = g.agent.GetPosition();
			const VisionConfig& v = g.agent.Vision();
			const float yaw  = glm::radians(g.agent.GetYaw());
			const float half = glm::radians(v.halfAngleDegrees);

			const ImU32 colMax    = ImGui::GetColorU32(ImVec4(0.35f, 0.5f, 0.9f, 0.45f));
			const ImU32 colStand  = ImGui::GetColorU32(ImVec4(0.30f, 0.85f, 1.0f, 0.9f));  // cyan
			const ImU32 colCrouch = ImGui::GetColorU32(ImVec4(0.45f, 0.95f, 0.55f, 0.9f)); // green
			const ImU32 colProne  = ImGui::GetColorU32(ImVec4(0.75f, 0.6f, 1.0f, 0.9f));   // violet
			const ImU32 colHear   = ImGui::GetColorU32(ImVec4(1.0f, 0.7f, 0.25f, 0.8f));
			const ImU32 colAware  = ImGui::GetColorU32(ImVec4(1.0f, 0.3f, 0.25f, 0.9f));
			const ImU32 colFacing = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.95f));

			// Max sight — now WITH a cone, so the full field of view is drawn even
			// when concealment has shrunk the live zones inside it.
			DrawSightZone(gpos, yaw, half, v.rangeMeters, colMax, 1.5f);

			// All three stance zones at once — stand / crouch / prone — so the
			// three detection distances are visible together. The zone matching
			// the player's CURRENT stance is drawn thicker so it stands out.
			const float sv = m_Character->GetStanceValue();
			struct Zone { float stance; ImU32 col; };
			const Zone zones[] = { { 0.0f, colStand }, { 1.0f, colCrouch }, { 2.0f, colProne } };
			for (const Zone& z : zones)
			{
				const bool current = std::fabs(sv - z.stance) < 0.5f;
				DrawSightZone(gpos, yaw, half, EffectiveSightForStance(g.agent, z.stance),
					z.col, current ? 3.5f : 2.0f);
			}

			// Close-range floor stays on the guard (it's a property of the guard).
			DrawGroundArc(gpos, v.minAwarenessRange, colAware, 2.0f);

			// Noise radiates from YOU, so the audible ring is centred on the
			// player: any guard inside it can hear you. Radius uses this guard's
			// hearing range, so it reads as "where a guard like this would hear
			// the sound I'm making right now".
			if (m_PlayerNoise > 0.01f)
				DrawGroundArc(m_Character->GetPosition(),
					v.hearingRange * m_PlayerNoise, colHear, 2.0f);

			// Facing stub from the guard's eye — unambiguous "looking that way".
			glm::vec3 eye = g.agent.GetEyePosition();
			DrawWorldLine(eye, eye + g.agent.GetForward() * 3.0f, colFacing, 3.0f);
			ImVec2 tip;
			if (WorldToScreen(eye + g.agent.GetForward() * 3.0f, tip))
				ImGui::GetBackgroundDrawList()->AddCircleFilled(tip, 4.0f, colFacing);
		}

		// The test range's actual instrument: every perception number, live, plus
		// sliders for the guard's vision/hearing so ranges can be dialled in
		// against the distance markers instead of guessed in a code constant.
		void DrawTestRangePanel()
		{
			if (m_Level != Level::TestRange || m_Guards.empty()) return;

			GuardSlot& g = m_Guards[0];

			ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(330.0f, 0.0f), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowBgAlpha(0.82f);

			if (ImGui::Begin("Test Range", nullptr, ImGuiWindowFlags_AlwaysAutoResize
				| ImGuiWindowFlags_NoFocusOnAppearing))
			{
				// The cursor is captured for mouselook while playing; you can't
				// click these sliders until you free it. Tab does that without
				// pausing, so the guard keeps reacting while you tune.
				if (m_DebugCursorFree)
					ImGui::TextColored(ImVec4(0.5f, 0.95f, 0.5f, 1.0f),
						"CURSOR FREE - Tab to look again");
				else
					ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
						"Press TAB to free cursor & edit");
				ImGui::Separator();

				glm::vec3 toGuard = m_Character->GetPosition() - g.agent.GetPosition();
				toGuard.y = 0.0f;
				const float distance = glm::length(toGuard);

				ImGui::Text("Distance to guard: %.1f m", distance);
				ImGui::Separator();

				// --- What the player currently is ---------------------------
				const char* stanceName =
					m_Character->IsProne()     ? "Prone" :
					m_Character->IsCrouching() ? "Crouch" : "Stand";
				ImGui::Text("Stance    %s (%.2f)", stanceName, m_Character->GetStanceValue());
				ImGui::Text("Speed     %.2f m/s", m_Character->GetHorizontalSpeed());
				ImGui::Text("Exposure  %.2f  (light)", m_PlayerExposure);
				ImGui::Text("Conceal   %.2f  (grass; 1 = none)", m_PlayerConcealment);
				ImGui::Text("Noise     %.2f", m_PlayerNoise);
				ImGui::Text("Motion    %.2f", ComputeMotion());

				// The single most useful derived number: how far this guard can
				// actually see you *right now*, given everything above.
				const float stanceRange = StanceMix(m_Character->GetStanceValue(),
					1.0f, kCrouchRangeMul, kProneRangeMul);
				const float shadowRange = glm::mix(kShadowRangeFloor, 1.0f, m_PlayerExposure);
				const float rangeExposure = stanceRange * shadowRange * m_PlayerConcealment;
				const float effectiveSight = std::max(
					g.agent.Vision().rangeMeters * rangeExposure,
					g.agent.Vision().minAwarenessRange);
				const float audibleRange = g.agent.Vision().hearingRange * m_PlayerNoise;

				ImGui::Separator();
				ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f),
					"Effective sight range: %.1f m", effectiveSight);
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.5f, 1.0f),
					"Audible out to:        %.1f m", audibleRange);
				ImGui::TextDisabled("(vs %.1f m actual distance)", distance);

				// --- What the guard currently perceives ----------------------
				ImGui::Separator();
				const char* state = "Unaware";
				switch (g.agent.GetAlertState())
				{
				case AlertState::Investigating: state = "Investigating"; break;
				case AlertState::Suspicious:    state = "Suspicious";    break;
				case AlertState::Alerted:       state = "ALERTED";       break;
				default: break;
				}
				ImGui::Text("Guard: %s  %s", state, g.agent.CanSeeTarget() ? "(sees you)" : "");
				glm::vec3 tint = g.agent.GetDebugTint();
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(tint.r, tint.g, tint.b, 1.0f));
				ImGui::ProgressBar(g.agent.GetDetection(), ImVec2(-1.0f, 14.0f));
				ImGui::PopStyleColor();

				// --- Live tuning --------------------------------------------
				ImGui::Separator();
				ImGui::TextDisabled("Guard tuning (live)");
				ImGui::SliderFloat("Sight range", &g.agent.Vision().rangeMeters, 5.0f, 60.0f, "%.0f m");
				ImGui::SliderFloat("Cone half-angle", &g.agent.Vision().halfAngleDegrees, 10.0f, 90.0f, "%.0f deg");
				ImGui::SliderFloat("Time to detect", &g.agent.Vision().timeToDetect, 0.2f, 5.0f, "%.2f s");
				ImGui::SliderFloat("Time to forget", &g.agent.Vision().timeToForget, 0.5f, 15.0f, "%.2f s");
				ImGui::SliderFloat("Hearing range", &g.agent.Vision().hearingRange, 0.0f, 60.0f, "%.0f m");
				ImGui::SliderFloat("Min awareness", &g.agent.Vision().minAwarenessRange, 0.0f, 10.0f, "%.1f m");

				if (ImGui::Button("Reset guard  [R]"))
				{
					g.agent.ResetPerception();
					g.agent.SetPosition(m_RangeGuardHome);
				}
				ImGui::SameLine();
				ImGui::Checkbox("Freeze", &m_FreezeRangeGuard);
				ImGui::SameLine();
				ImGui::Checkbox("Overlay", &m_ShowRangeOverlay);

				ImGui::Separator();
				ImGui::TextDisabled("Overlay legend (cone shown on each)");
				ImGui::TextColored(ImVec4(0.30f, 0.85f, 1.0f, 1.0f),  "  cyan    sight range if STANDING");
				ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "  green   sight range if CROUCHED");
				ImGui::TextColored(ImVec4(0.75f, 0.6f, 1.0f, 1.0f),   "  violet  sight range if PRONE");
				ImGui::TextColored(ImVec4(0.35f, 0.5f, 0.9f, 1.0f),   "  blue    max range (unconcealed)");
				ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.25f, 1.0f),   "  amber   your noise radius (around you)");
				ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.25f, 1.0f),   "  red     min awareness (ignores cover)");
				ImGui::TextDisabled("  white   facing   (current stance = thick)");

				ImGui::Separator();
				ImGui::TextDisabled("Markers every 5 m (tall = 10 m).");
				ImGui::TextDisabled("Grass lane is to the right; lit pool at 15 m.");
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

			// World collision. Ignores the player's own body index, which is
			// resolved lazily: the index doesn't exist yet at this point, and
			// Scene::AddPrimitive reallocates, so it must not be captured early.
			m_Character->SetCollisionResolver(
				[this](const glm::vec3& feet, float radius, float height, float stepHeight)
				{
					return m_Scene->ResolveCharacterCollision(feet, radius, height,
						stepHeight, m_BodyIndex);
				});

			// Lets the player stand on top of crates and walls, not just on the
			// terrain heightmap.
			m_Character->SetSupportSampler(
				[this](const glm::vec3& feet, float radius, float maxRise)
				{
					return m_Scene->SampleSupportHeight(feet, radius, maxRise, m_BodyIndex);
				});

			// Climbing: walls and crates become routes.
			m_Character->SetLedgeProbe(
				[this](const glm::vec3& feet, const glm::vec3& facing, float reach,
					float maxHeight, float headroom, glm::vec3& outLedge)
				{
					return m_Scene->FindLedge(feet, facing, reach, maxHeight, headroom,
						m_Character->GetConfig().radius, outLedge, m_BodyIndex);
				});

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

			m_Guards.resize(kGuardCount);
			for (int i = 0; i < kGuardCount; ++i)
			{
				m_Guards[i].sceneIndex = AddPlaceholder("Guard");

				// A torch prop + light per guard, both off until the guard is a
				// designated bearer. Allocated once so restarts/alerts just flip
				// flags instead of stacking lights.
				m_Guards[i].torchPropIndex = AddPlaceholder("Torch");
				Light* torch = m_Scene->AddLight("Torch", LightType::Point);
				torch->color = glm::vec3(1.0f, 0.6f, 0.25f);
				torch->intensity = 1.8f;
				torch->radius = kTorchRadius;
				torch->enabled = false;
				m_Guards[i].torchLightIndex = m_Scene->GetLightCount() - 1;
			}

			m_RelicIndex = AddPlaceholder("Relic");
			m_StoneIndex = AddPlaceholder("Stone");

			// Braziers: a visible fire marker + a real point light each. Allocated
			// ONCE (positioned per-build in BuildCamp) so restarts don't stack
			// duplicate lights. The lights genuinely illuminate nearby walls — the
			// camp reads as a lit night camp — and their pools are what the shadow
			// concealment system reads as "exposed".
			for (int i = 0; i < kBrazierCount; ++i)
			{
				m_BrazierPropIndices[i] = AddPlaceholder("Brazier");
				Light* light = m_Scene->AddLight("Brazier", LightType::Point);
				light->color = glm::vec3(1.0f, 0.55f, 0.22f);   // firelight
				light->intensity = 2.2f;
				light->radius = kBrazierRadius;
				m_BrazierLightIndices[i] = m_Scene->GetLightCount() - 1;
			}

			// Actors must not occlude sight rays aimed at themselves — see
			// SceneObject::blocksSight. Without this the guard's ray to the
			// player stops inside the player's own body box and it never sees
			// anything.
			//
			// They also don't block movement yet: character-vs-character
			// collision needs push-apart rules we haven't designed, and a solid
			// bike would fight the mount radius. Cover blocks; actors don't.
			auto makeActor = [this](int idx)
			{
				if (SceneObject* o = SceneObjectAt(idx))
				{
					o->blocksSight = false;
					o->blocksMovement = false;
				}
			};
			makeActor(m_BodyIndex);
			makeActor(m_BikeIndex);
			makeActor(m_RelicIndex);
			makeActor(m_StoneIndex);
			for (const auto& g : m_Guards) { makeActor(g.sceneIndex); makeActor(g.torchPropIndex); }
			for (int i = 0; i < kBrazierCount; ++i) makeActor(m_BrazierPropIndices[i]);

			// Camp structure: walls and crates. These are the only things that
			// block sight and movement, so the whole level is built from them.
			m_StructureIndices.resize(kStructureCount);
			for (int i = 0; i < kStructureCount; ++i)
				m_StructureIndices[i] = AddPlaceholder("Structure");

			// Grass patches: concealment props (don't block sight or movement).
			for (int i = 0; i < kGrassCount; ++i)
			{
				m_GrassPropIndices[i] = AddPlaceholder("Grass");
				makeActor(m_GrassPropIndices[i]);
			}
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
			// The kind tag is load-bearing, not decoration: without it the object
			// has no known bounds and Scene::Raycast skips it entirely, so cover
			// would render but never block a sight line.
			m_Scene->AddPrimitive(name, std::move(mesh), PrimitiveKind::TestCube);
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
			// The bike is hidden in the range; don't let an invisible prop be
			// mountable just because you walked over where it used to be.
			if (m_Level != Level::Camp)
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

				BuildLevel(m_Character->GetPosition(), m_Character->GetForward());

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
				UpdateStanceInput(deltaTime, in);   // fills in.crouch / in.prone
				in.jumpPressed = GetInput()->IsPressed(InputCode::Key_Space);

				// Hold Q to aim (shows the arc and who it will pull), release to
				// throw. Aiming rather than instant-throw is what turns the
				// stone from a guess into a plan.
				const bool aimingNow = GetInput()->IsDown(InputCode::Key_Q);
				if (m_AimingStone && !aimingNow)
					ThrowStone();
				m_AimingStone = aimingNow;

				// Hold F to survey the camp. Held, not toggled: reading the
				// board should cost you time standing still.
				m_TrackingHeld = GetInput()->IsDown(InputCode::Key_F);

				in.throttle        = GetInput()->IsDown(InputCode::Key_W) ? 1.0f : 0.0f;
				in.brake           = GetInput()->IsDown(InputCode::Key_S) ? 1.0f : 0.0f;
				in.steer           = in.move.x;
				in.interactPressed = GetInput()->IsPressed(InputCode::Key_E);
			}

			// R: in the range it re-arms the guard for another measurement (the
			// mission never ends there, so the key is free); in the camp it's
			// the retry after a resolved run.
			if (controlling && GetInput()->IsPressed(InputCode::Key_R))
			{
				if (m_Level == Level::TestRange)
				{
					if (!m_Guards.empty())
					{
						m_Guards[0].agent.ResetPerception();
						m_Guards[0].agent.SetPosition(m_RangeGuardHome);
					}
				}
				else if (MissionOver())
				{
					RestartMission();
					return;
				}
			}

			if (in.interactPressed)
				Interact();

			m_Possessed->Update(in, deltaTime);

			// The bike keeps hovering (and settling onto the terrain) while parked, so
			// it doesn't sink or hang in the air after you step off.
			if (!IsRiding())
				m_Bike->Update(PawnInput{}, deltaTime);

			// The guard perceives the CHARACTER, not the camera and not the bike:
			// riding past should be seen as the rider, and a free-orbiting camera
			// must never be what gets you spotted.
			//
			// Timed because a frame-rate complaint was pinned on the detection
			// mechanic; measuring beats guessing. Offline this path benchmarks at
			// ~0.8us, so if this line ever shows milliseconds the cause is
			// something the benchmark didn't model, not the raycast math.
			{
				auto t0 = std::chrono::high_resolution_clock::now();

				// How exposed the player is to LIGHT, computed once and shared by
				// every guard (it's a property of the player, not the observer).
				m_PlayerExposure = ComputeExposure(m_Character->GetCameraFocus());
				m_PlayerConcealment = ComputeConcealment(m_Character->GetPosition());

				// Crouching lowers where the guard aims (so low cover blocks)
				// and shrinks the silhouette (so detection accrues slower); shadow
				// lowers both the detection rate and the range at which you
				// register at all. Both come off smoothed values so they ramp.
				PerceptionTarget target;
				target.position = m_Character->GetPosition();
				target.height = m_Character->GetConfig().eyeHeight
					* m_Character->GetHeightFraction();

				// Stance drives both levers: lower posture = harder to see AND
				// seen from less far. Standing keeps full range (per the user's
				// "guards should see me further when standing"). Piecewise across
				// stand/crouch/prone by the smoothed stance value.
				const float sv = m_Character->GetStanceValue();
				const float stanceVis   = StanceMix(sv, 1.0f, kCrouchVisibility, kProneVisibility);
				const float stanceRange = StanceMix(sv, 1.0f, kCrouchRangeMul,   kProneRangeMul);

				const float shadowRate  = glm::mix(kShadowRateFloor,  1.0f, m_PlayerExposure);
				const float shadowRange = glm::mix(kShadowRangeFloor, 1.0f, m_PlayerExposure);

				target.visibility    = stanceVis   * shadowRate  * m_PlayerConcealment;
				target.rangeExposure = stanceRange * shadowRange * m_PlayerConcealment;
				target.motion        = ComputeMotion();
				target.noise         = ComputeNoise();
				m_PlayerNoise        = target.noise;   // for the HUD

				// Guards keep patrolling after the mission resolves — a camp
				// that freezes on the win screen looks broken — but dead ones
				// stop, and a resolved mission can't change state again.
				for (auto& slot : m_Guards)
					if (slot.alive)
						slot.agent.Update(deltaTime, target);

				ApplyRangeGuardFreeze();

				auto t1 = std::chrono::high_resolution_clock::now();
				float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
				m_GuardMsAccum += ms;
				m_GuardMsPeak = std::max(m_GuardMsPeak, ms);
				++m_GuardSamples;
			}

			UpdateStone(deltaTime);
			UpdateBodyDiscovery(deltaTime);
			UpdateTorches();
			UpdateMission(deltaTime);
			LogGpuBreakdown(deltaTime);

			m_CameraRig.UpdateCamera(*m_Camera, m_Possessed->GetCameraFocus(), &m_GroundFn);
			SyncBodies();
		}

		//----------------------------------------------------------------------
		// The camp (ROADMAP 3.4).
		//
		// Laid out relative to where the player spawns rather than at fixed
		// world coordinates, because the spawn point follows the terrain and a
		// hardcoded camp would end up buried in a hill.
		//
		// LAYOUT INTENT — this is a level, not a box of props:
		//  - A walled compound with ONE gap. The gap is the obvious way in and
		//    it's watched, so the interesting question is whether you take it.
		//  - Crates OUTSIDE the walls give an approach with cover, so the first
		//    30 seconds are about reading patrol timing rather than sprinting.
		//  - Crates INSIDE break the compound's interior sight lines, so a
		//    guard standing still doesn't dominate the whole space.
		//  - Three guards: one walks the perimeter (the timing puzzle), one
		//    circles the relic (the close-range problem), one holds near the
		//    gate (so the obvious entrance has a cost).
		//  - The relic sits at the centre, in the open. Reaching it should
		//    require solving the interior, not just walking in.
		//----------------------------------------------------------------------
		//----------------------------------------------------------------------
		// CAMP GEOMETRY — a lane plan, not a pile of boxes.
		//
		// The previous layout put walls and patrol routes down independently,
		// so routes sliced straight through walls. Guards now collide, but a
		// guard grinding along a wall it was supposed to walk around is still
		// broken — the fix has to be the LAYOUT.
		//
		// So the camp is defined as concentric rings with reserved lanes, and
		// every route runs down a lane:
		//
		//   r = 18     outer wall            (gates: front at f=-18, back at r=-18)
		//   r = 15     PERIMETER LANE        - kept clear all the way round
		//   r = 11.5   INNER LANE            - kept clear all the way round
		//   r = 8      inner wall            (doorways at the middle of each side)
		//   r = 4.5    COURTYARD LANE        - kept clear
		//   r = 0      relic
		//
		// Props may only sit in the bands BETWEEN lanes (~9-10.5, ~13-14, ~16.5+).
		// Anything placed on a lane will be walked into, so keep them clear.
		//----------------------------------------------------------------------
		static constexpr float kOuterWall = 18.0f;
		static constexpr float kPerimeterLane = 15.0f;
		static constexpr float kInnerLane = 11.5f;
		static constexpr float kInnerWall = 8.0f;
		static constexpr float kCourtLane = 4.5f;

		static constexpr int kStructureCount = 25;

		void BuildCamp(const glm::vec3& playerPos, const glm::vec3& playerForward)
		{
			// Lock the layout axis on the first build so retries are identical.
			if (!m_CampBuilt)
			{
				glm::vec3 f = glm::vec3(playerForward.x, 0.0f, playerForward.z);
				if (glm::dot(f, f) > 1e-6f) m_CampForward = glm::normalize(f);
			}
			const glm::vec3 fwd = m_CampForward;
			const glm::vec3 right = glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f));

			// Camp centre, well ahead of the player. A long approach (~37 m to
			// the front gate) so there's time to read patrols and pick a way in
			// before you're anywhere near a guard.
			const glm::vec3 center = playerPos + fwd * 55.0f;

			auto OnGround = [&](glm::vec3 p) { p.y = m_GroundFn(p.x, p.z); return p; };
			// Camp-local (right, forward) -> world.
			auto At = [&](float r, float f) { return OnGround(center + right * r + fwd * f); };

			m_ExtractPoint = playerPos;
			m_RelicHome = At(0.0f, 0.0f);

			// --- Structure ----------------------------------------------------
			// Walls are 3.5 m tall: above step height (so they can't be walked
			// onto) and above eye height (so they properly block sight).
			const float kWallH = 3.5f;
			const float kCrateH = 1.6f;

			struct Piece { float r, f; glm::vec3 scale; };
			const Piece pieces[kStructureCount] = {
				// --- Outer wall (r = 18), two ways in ------------------------
				// Front gate: short approach, heavily watched. Back gate: a much
				// longer walk with far less traffic. Two entrances make the
				// approach a choice instead of a queue.
				{   0.0f,  18.0f, { 37.0f, kWallH,  1.0f } },   // far wall, solid
				{  18.0f,   0.0f, {  1.0f, kWallH, 37.0f } },   // right wall, solid
				{ -18.0f,  11.0f, {  1.0f, kWallH, 15.0f } },   // left wall, far half
				{ -18.0f, -11.0f, {  1.0f, kWallH, 15.0f } },   // left wall, near half
				//                                    back gate: f in [-3.5, 3.5]
				{ -11.5f, -18.0f, { 14.0f, kWallH,  1.0f } },   // near wall, left of gate
				{  11.5f, -18.0f, { 14.0f, kWallH,  1.0f } },   // near wall, right of gate
				//                                    front gate: r in [-4.5, 4.5]

				// --- Inner wall (r = 8), a doorway in the middle of each side -
				// Splits the compound so no single guard watches everything, and
				// gives corners to take someone down behind. Each side is two
				// segments with a 4 m doorway between them, and every doorway is
				// centred so the courtyard lanes line up with the outer lanes.
				{  -5.5f,   8.0f, {  5.0f, kWallH,  1.0f } },   // far side, left
				{   5.5f,   8.0f, {  5.0f, kWallH,  1.0f } },   // far side, right
				{  -5.5f,  -8.0f, {  5.0f, kWallH,  1.0f } },   // near side, left
				{   5.5f,  -8.0f, {  5.0f, kWallH,  1.0f } },   // near side, right
				{  -8.0f,  -5.5f, {  1.0f, kWallH,  5.0f } },   // left side, near
				{  -8.0f,   5.5f, {  1.0f, kWallH,  5.0f } },   // left side, far
				{   8.0f,  -5.5f, {  1.0f, kWallH,  5.0f } },   // right side, near
				{   8.0f,   5.5f, {  1.0f, kWallH,  5.0f } },   // right side, far

				// --- Props: ONLY in the bands between lanes ------------------
				// Band at ~9.5 (between the inner wall at 8 and inner lane 11.5)
				{  -9.7f,  -9.7f, {  2.0f, kCrateH, 2.0f } },
				{   9.7f,   9.7f, {  2.0f, kCrateH, 2.0f } },
				{   9.7f,  -9.7f, {  2.0f, kCrateH, 2.0f } },

				// Band at ~13.3 (between inner lane 11.5 and perimeter lane 15)
				{ -13.3f,   0.0f, {  2.0f, kCrateH, 3.0f } },
				{  13.3f,   4.0f, {  2.0f, kCrateH, 3.0f } },

				// Band at ~16.5 (between perimeter lane 15 and outer wall 18):
				// climb crates, tall enough to mantle onto the outer wall.
				{  16.5f, -10.0f, {  2.0f,  2.4f,  2.5f } },
				{ -16.5f,   9.0f, {  2.0f,  2.4f,  2.5f } },

				// --- Approach cover, outside the camp ------------------------
				{  -8.0f, -26.0f, {  4.0f, kCrateH, 2.0f } },
				{   7.0f, -24.0f, {  3.0f, kCrateH, 3.0f } },
				{ -26.0f,  -4.0f, {  3.0f, kCrateH, 4.0f } },   // on the back route
				{ -25.0f,   7.0f, {  2.5f, kCrateH, 2.5f } },
			};

			for (int i = 0; i < kStructureCount; ++i)
			{
				SceneObject* obj = SceneObjectAt(m_StructureIndices[i]);
				if (!obj || !obj->meshDrawable) continue;

				glm::vec3 pos = At(pieces[i].r, pieces[i].f);
				// Cube geometry is centred on its origin, so lift by half the
				// height to sit the piece on the ground instead of half-buried.
				pos.y += pieces[i].scale.y * 0.5f;

				obj->meshDrawable->SetTransform(
					glm::translate(glm::mat4(1.0f), pos) *
					glm::scale(glm::mat4(1.0f), pieces[i].scale));
			}

			// --- Braziers -----------------------------------------------------
			// Placed to light the doorways and key posts and to leave the wall
			// shadows dark — so hugging the perimeter is genuinely stealthier
			// than crossing the lit courtyard.
			const glm::vec2 brazierAt[kBrazierCount] = {
				{  0.0f, -kInnerWall + 1.0f },   // front gate, inside
				{  0.0f,  0.0f },                // dead centre, by the relic (lights the prize)
				{ -kInnerWall - 1.0f, 0.0f },    // back-gate approach
				{  kInnerWall + 1.0f, 0.0f },    // right doorway
				{  0.0f,  kInnerWall + 2.0f },   // far courtyard
				{  0.0f, -kOuterWall + 2.0f },   // just inside the front gate
			};

			for (int i = 0; i < kBrazierCount; ++i)
			{
				glm::vec3 ground = At(brazierAt[i].x, brazierAt[i].y);

				if (m_BrazierLightIndices[i] < m_Scene->GetLightCount())
					m_Scene->GetLight(m_BrazierLightIndices[i])->position =
						ground + glm::vec3(0.0f, 1.6f, 0.0f);

				if (SceneObject* fire = SceneObjectAt(m_BrazierPropIndices[i]))
					if (fire->meshDrawable)
					{
						fire->meshDrawable->SetTransform(
							glm::translate(glm::mat4(1.0f), ground + glm::vec3(0.0f, 0.7f, 0.0f)) *
							glm::scale(glm::mat4(1.0f), glm::vec3(0.35f, 1.2f, 0.35f)));
						fire->meshDrawable->SetCustomData(glm::vec4(1.0f, 0.6f, 0.2f, 1.0f));
					}
			}

			// --- Grass (crawl routes) -----------------------------------------
			// Laid to reward crawling: a long approach patch (belly-crawl toward
			// the front instead of walking up exposed), flanking strips along the
			// perimeter, and a patch on the last stretch to the relic. Each is a
			// flat green marker + a horizontal conceal region.
			m_ConcealVolumes.clear();
			struct GrassPatch { float r, f, halfW, halfD; };
			const GrassPatch grass[kGrassCount] = {
				{  0.0f, -30.0f, 10.0f, 5.0f },   // wide approach field, front
				{ -14.0f, -24.0f, 4.0f, 6.0f },   // approach, left flank
				{ -24.0f,  0.0f,  4.0f, 7.0f },   // back-gate approach
				{  16.0f, -22.0f, 3.0f, 5.0f },   // approach, right flank
				{ -13.0f,  4.0f,  3.0f, 5.0f },   // inside, left lane
				{  13.0f, -3.0f,  3.0f, 5.0f },   // inside, right lane
				{  0.0f,  -4.0f,  3.5f, 2.5f },   // last stretch to the relic
			};

			for (int i = 0; i < kGrassCount; ++i)
			{
				glm::vec3 world = At(grass[i].r, grass[i].f);
				m_ConcealVolumes.push_back({
					glm::vec2(world.x, world.z),
					glm::vec2(grass[i].halfW, grass[i].halfD) });

				if (SceneObject* g = SceneObjectAt(m_GrassPropIndices[i]))
					if (g->meshDrawable)
					{
						g->meshDrawable->SetTransform(
							glm::translate(glm::mat4(1.0f), world + glm::vec3(0.0f, 0.5f, 0.0f)) *
							glm::scale(glm::mat4(1.0f),
								glm::vec3(grass[i].halfW * 2.0f, 1.0f, grass[i].halfD * 2.0f)));
						g->meshDrawable->SetCustomData(glm::vec4(0.18f, 0.4f, 0.15f, 1.0f));
					}
			}

			// --- Guards -------------------------------------------------------
			// Eight guards, but the point is that they do DIFFERENT jobs. Eight
			// identical patrollers is the same waiting game eight times over;
			// what makes a camp readable is that each guard poses its own
			// question, and that their timings don't line up into one rhythm.
			struct GuardPost
			{
				std::vector<PatrolPoint> route;
				float speed;        // m/s
				float pause;        // seconds at each waypoint
				float coneDegrees;  // half-angle
				float range;        // metres
				bool  pingPong;
			};

			// A post: stand here, watch THAT. Giving stops an intent is what
			// stops patrols reading as "walk to an arbitrary coordinate, stare
			// at the next arbitrary coordinate".
			auto Watch = [&](float r, float f, float lookR, float lookF, float hold = -1.0f)
			{
				return PatrolPoint(At(r, f), At(lookR, lookF), hold);
			};
			auto Walk = [&](float r, float f) { return PatrolPoint(At(r, f)); };

			// EVERY route below runs down a reserved lane or through a doorway.
			// Nothing cuts across a wall — that was the "guards phase through
			// geometry" bug, and collision alone wouldn't have fixed it (a guard
			// grinding along a wall it should be walking around is still wrong).
			const float P = kPerimeterLane;   // 15.0  — outer ring, clear
			const float I = kInnerLane;       // 11.5  — inner ring, clear
			const float C = kCourtLane;       //  4.5  — courtyard ring, clear

			const std::vector<GuardPost> posts = {
				// 1. Gate sentry — paces inside the front gate and, crucially,
				//    LOOKS AT THE GATE while stopped. Long holds, so the front
				//    door costs real patience.
				{ { Watch(-3.0f, -P, -3.0f, -kOuterWall, 4.0f),
					Watch(3.0f, -P,  3.0f, -kOuterWall, 4.0f) },
				  1.2f, 3.5f, 60.0f, 28.0f, true },

				// 2. Perimeter lap, clockwise — the long timing puzzle. Pauses
				//    at each corner looking down the next straight, which is
				//    where a player would be crossing.
				{ { Watch(-P, -P,  P, -P), Watch(P, -P,  P,  P),
					Watch(P,  P, -P,  P), Watch(-P,  P, -P, -P) },
				  2.6f, 1.0f, 40.0f, 32.0f, false },

				// 3. Inner lap, ANTICLOCKWISE — deliberately counter-rotating,
				//    so the two rings' safe windows drift against each other
				//    instead of settling into one memorisable rhythm.
				{ { Watch(I,  I, -I,  I), Watch(-I,  I, -I, -I),
					Watch(-I, -I,  I, -I), Watch(I, -I,  I,  I) },
				  2.0f, 1.5f, 45.0f, 26.0f, false },

				// 4. Relic guard — circles the prize, always looking INWARD at
				//    it. Means approaching the relic is covered from every side,
				//    and the only opening is directly behind them.
				{ { Watch(-C, -C, 0.0f, 0.0f), Watch(C, -C, 0.0f, 0.0f),
					Watch(C,  C, 0.0f, 0.0f), Watch(-C,  C, 0.0f, 0.0f) },
				  1.8f, 2.0f, 50.0f, 23.0f, false },

				// 5. Back-gate watcher — watches the rear entrance, so the
				//    "quiet" way in isn't free.
				{ { Watch(-P, -3.0f, -kOuterWall, -3.0f, 3.0f),
					Watch(-P,  4.0f, -kOuterWall,  4.0f, 3.0f) },
				  1.5f, 2.5f, 55.0f, 26.0f, true },

				// 6. West spur — runs the inner lane's left straight, checking
				//    the doorway at each end.
				{ { Watch(-I, -8.0f, -kInnerWall, -8.0f),
					Watch(-I,  8.0f, -kInnerWall,  8.0f) },
				  2.2f, 1.2f, 42.0f, 25.0f, true },

				// 7. East spur — the same on the right, offset in length so it
				//    falls out of phase with its opposite number.
				{ { Watch(I, -6.0f, kInnerWall, -6.0f),
					Watch(I, 10.0f, kInnerWall, 10.0f) },
				  2.4f, 0.8f, 42.0f, 25.0f, true },

				// 8. Rover — the only guard that crosses between rings, and it
				//    does so THROUGH the doorways (r = 0 is a doorway on both
				//    the near and far inner walls). Sweeps the camp north-south,
				//    disturbing the rhythms the others make predictable. Pauses
				//    in the courtyard looking back at the relic.
				{ { Walk(0.0f, -I), Watch(0.0f, -C, 0.0f, 0.0f),
					Watch(0.0f,  C, 0.0f, 0.0f), Walk(0.0f,  I) },
				  2.3f, 1.8f, 38.0f, 27.0f, true },
			};

			for (int i = 0; i < kGuardCount; ++i)
			{
				GuardSlot& slot = m_Guards[i];
				const GuardPost& post = posts[i % posts.size()];

				slot.alive = true;
				slot.bodyFound = false;

				slot.agent = GuardAgent{};   // reset perception + patrol state
				slot.agent.SetGroundSampler(m_GroundFn);

				// Perception asks the scene; the guard itself knows nothing about
				// Scene, Renderer or terrain. Actors are excluded via blocksSight,
				// so this only trips on real world geometry.
				slot.agent.SetLineOfSightFn(
					[this](const glm::vec3& from, const glm::vec3& to)
					{
						return m_Scene->HasLineOfSight(from, to, m_GroundFn);
					});

				// Guards collide with the camp, so a carelessly placed waypoint
				// slides them along a wall instead of through it.
				slot.agent.SetCollisionResolver(
					[this](const glm::vec3& feet, float radius, float height)
					{
						return m_Scene->ResolveCharacterCollision(feet, radius, height, 0.45f, -1);
					});

				slot.agent.Patrol().moveSpeed = post.speed;
				slot.agent.Patrol().pauseAtWaypoint = post.pause;
				slot.agent.Patrol().pingPong = post.pingPong;
				slot.agent.Vision().halfAngleDegrees = post.coneDegrees;
				slot.agent.Vision().rangeMeters = post.range;

				slot.agent.SetPatrolRoute(post.route);

				// Designated torch-bearers: the gate sentry (0), the back-gate
				// watcher (4), and the rover (7). Three of eight — the entrances
				// are lit and patrolled by light, the interior mostly is not, so
				// the dark lanes are the stealth routes. More get lit on alert.
				slot.carriesTorch = (i == 0 || i == 4 || i == 7);
			}

			m_StonesLeft = kStoneCount;
			m_Stone.active = false;

			m_Mission = Mission::Infiltrate;
			m_RelicSpin = 0.0f;
			m_CampBuilt = true;

			// Put the relic back on its plinth — a retry after a successful
			// steal would otherwise start with nothing to steal.
			if (SceneObject* o = SceneObjectAt(m_RelicIndex))
				o->visible = true;
		}

		// Which layout is currently built. The camp is the game; the test range
		// is a controlled instrument for tuning perception. Both reuse the SAME
		// pre-allocated object pools, so switching is a rebuild, not allocation.
		// (Declared here because SwitchLevel's signature needs the type.)
		enum class Level { Camp, TestRange };
		Level     m_Level = Level::Camp;
		glm::vec3 m_RangeGuardHome{ 0.0f };
		bool      m_FreezeRangeGuard = true;    // measurements need a still guard
		bool      m_ShowRangeOverlay = true;    // cone + range rings
		bool      m_DebugCursorFree = false;    // Tab: free cursor for the panel, game running

		//----------------------------------------------------------------------
		// TEST RANGE — a controlled instrument, not a level.
		//
		// The camp is the worst possible place to tune perception: everything
		// moves and nothing is measured. Here: one stationary guard looking down
		// a lane with distance markers every 5 m, parallel bare / grass strips so
		// surfaces can be compared at identical distances, and a lit pool vs
		// darkness. Walk backwards and watch exactly where you drop out of sight.
		//----------------------------------------------------------------------
		void BuildTestRange(const glm::vec3& playerPos, const glm::vec3& playerForward)
		{
			if (!m_CampBuilt)
			{
				glm::vec3 f = glm::vec3(playerForward.x, 0.0f, playerForward.z);
				if (glm::dot(f, f) > 1e-6f) m_CampForward = glm::normalize(f);
			}
			const glm::vec3 fwd = m_CampForward;
			const glm::vec3 right = glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f));

			auto OnGround = [&](glm::vec3 p) { p.y = m_GroundFn(p.x, p.z); return p; };
			// Range-local: r = across the lane, f = distance down it from the guard.
			const glm::vec3 origin = playerPos + fwd * 6.0f;
			auto At = [&](float r, float f) { return OnGround(origin + right * r + fwd * f); };

			m_ExtractPoint = playerPos;
			m_RelicHome = At(0.0f, 3.0f);

			// --- Distance markers, every 5 m out to 40 -----------------------
			// Set off to the side so they never occlude the lane itself.
			int piece = 0;
			for (int d = 5; d <= 40 && piece < kStructureCount; d += 5, ++piece)
			{
				if (SceneObject* o = SceneObjectAt(m_StructureIndices[piece]))
					if (o->meshDrawable)
					{
						// Taller every 10 m so distances are countable at a glance.
						const float h = (d % 10 == 0) ? 2.0f : 1.0f;
						glm::vec3 p = At(-12.0f, static_cast<float>(d));
						p.y += h * 0.5f;
						o->meshDrawable->SetTransform(
							glm::translate(glm::mat4(1.0f), p) *
							glm::scale(glm::mat4(1.0f), glm::vec3(0.4f, h, 0.4f)));
						o->meshDrawable->SetCustomData(glm::vec4(0.9f, 0.9f, 0.95f, 1.0f));
					}
			}

			// --- Occluders: a wall to test corner-peeking and sound-through ---
			const struct { float r, f; glm::vec3 scale; } walls[] = {
				{  6.0f, 14.0f, { 1.0f, 3.5f, 10.0f } },   // long wall beside the lane
				{ -4.0f, 26.0f, { 8.0f, 3.5f,  1.0f } },   // wall across it, further out
			};
			for (const auto& w : walls)
			{
				if (piece >= kStructureCount) break;
				if (SceneObject* o = SceneObjectAt(m_StructureIndices[piece]))
					if (o->meshDrawable)
					{
						glm::vec3 p = At(w.r, w.f);
						p.y += w.scale.y * 0.5f;
						o->meshDrawable->SetTransform(
							glm::translate(glm::mat4(1.0f), p) *
							glm::scale(glm::mat4(1.0f), w.scale));
						o->meshDrawable->SetCustomData(glm::vec4(0.5f, 0.5f, 0.55f, 1.0f));
					}
				++piece;
			}

			// Park every unused structure far below the world.
			for (; piece < kStructureCount; ++piece)
				if (SceneObject* o = SceneObjectAt(m_StructureIndices[piece]))
					if (o->meshDrawable)
						o->meshDrawable->SetTransform(
							glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -500.0f, 0.0f)));

			// --- Surface strip: grass lane parallel to the bare lane ---------
			// Same distances, different surface, so concealment and noise can be
			// compared directly by walking one then the other.
			m_ConcealVolumes.clear();
			for (int i = 0; i < kGrassCount; ++i)
			{
				SceneObject* g = SceneObjectAt(m_GrassPropIndices[i]);
				if (!g || !g->meshDrawable) continue;

				if (i < 4)   // four patches tiling a 40 m grass lane at r = +6
				{
					const float segLen = 10.0f;
					const float centreF = 5.0f + segLen * i + segLen * 0.5f;
					glm::vec3 world = At(6.0f, centreF);

					m_ConcealVolumes.push_back({
						glm::vec2(world.x, world.z), glm::vec2(2.5f, segLen * 0.5f) });

					g->visible = true;
					g->meshDrawable->SetTransform(
						glm::translate(glm::mat4(1.0f), world + glm::vec3(0.0f, 0.5f, 0.0f)) *
						glm::scale(glm::mat4(1.0f), glm::vec3(5.0f, 1.0f, segLen)));
					g->meshDrawable->SetCustomData(glm::vec4(0.18f, 0.4f, 0.15f, 1.0f));
				}
				else
				{
					g->visible = false;
				}
			}

			// --- Light: one lit pool mid-lane, the rest dark ------------------
			for (int i = 0; i < kBrazierCount; ++i)
			{
				const bool used = (i == 0);
				if (m_BrazierLightIndices[i] < m_Scene->GetLightCount())
				{
					Light* light = m_Scene->GetLight(m_BrazierLightIndices[i]);
					light->enabled = used;
					if (used) light->position = At(0.0f, 15.0f) + glm::vec3(0.0f, 1.6f, 0.0f);
				}
				if (SceneObject* fire = SceneObjectAt(m_BrazierPropIndices[i]))
				{
					fire->visible = used;
					if (used && fire->meshDrawable)
					{
						fire->meshDrawable->SetTransform(
							glm::translate(glm::mat4(1.0f), At(0.0f, 15.0f) + glm::vec3(0.0f, 0.7f, 0.0f)) *
							glm::scale(glm::mat4(1.0f), glm::vec3(0.35f, 1.2f, 0.35f)));
						fire->meshDrawable->SetCustomData(glm::vec4(1.0f, 0.6f, 0.2f, 1.0f));
					}
				}
			}

			// --- One stationary guard, looking down the lane -----------------
			// A single-waypoint route keeps it planted (UpdatePatrol needs two
			// points to move), so detection is purely a function of YOUR state.
			for (int i = 0; i < kGuardCount; ++i)
			{
				GuardSlot& slot = m_Guards[i];
				slot.agent = GuardAgent{};
				slot.alive = (i == 0);
				slot.bodyFound = false;
				slot.carriesTorch = false;

				if (SceneObject* o = SceneObjectAt(slot.sceneIndex))
					o->visible = (i == 0);

				if (i != 0) continue;

				slot.agent.SetGroundSampler(m_GroundFn);
				slot.agent.SetLineOfSightFn(
					[this](const glm::vec3& from, const glm::vec3& to)
					{ return m_Scene->HasLineOfSight(from, to, m_GroundFn); });

				slot.agent.SetPatrolRoute(std::vector<glm::vec3>{ At(0.0f, 0.0f) });
				slot.agent.SetPosition(At(0.0f, 0.0f));
				// Face straight down the lane (+f).
				slot.agent.SetYaw(glm::degrees(std::atan2(fwd.z, fwd.x)));
			}

			m_RangeGuardHome = At(0.0f, 0.0f);

			m_StonesLeft = 99;              // unlimited-ish for experimenting
			m_Stone.active = false;
			m_Mission = Mission::Infiltrate; // no win/lose in the range
			m_CampBuilt = true;

			// Clear the distractions: no objective, no vehicle. The range should
			// contain nothing but the thing being measured.
			if (SceneObject* o = SceneObjectAt(m_RelicIndex)) o->visible = false;
			if (SceneObject* o = SceneObjectAt(m_BikeIndex))  o->visible = false;
		}

		// Pin the range guard in place. A guard that wanders off to investigate
		// ruins a measurement — you want it rotating and perceiving, but never
		// changing the distance you're reading off the markers.
		void ApplyRangeGuardFreeze()
		{
			if (m_Level != Level::TestRange || !m_FreezeRangeGuard || m_Guards.empty()) return;
			m_Guards[0].agent.SetPosition(m_RangeGuardHome);
		}

		void BuildLevel(const glm::vec3& playerPos, const glm::vec3& playerForward)
		{
			if (m_Level == Level::TestRange) BuildTestRange(playerPos, playerForward);
			else                             BuildCamp(playerPos, playerForward);
		}

		// Swap layouts in place: reset the player to the spawn and rebuild.
		void SwitchLevel(Level level)
		{
			m_Level = level;
			m_CampBuilt = false;   // let the new layout re-lock its own axis

			// The debug cursor only exists in the range; recapture on the way out.
			if (level != Level::TestRange && m_DebugCursorFree)
			{
				m_DebugCursorFree = false;
				GetWindow()->SetRelativeMouseMode(true);
			}

			glm::vec3 spawn = m_ExtractPoint;
			spawn.y = m_GroundFn(spawn.x, spawn.z);
			m_Character->SetPosition(spawn);
			Possess(m_Character.get());

			for (auto& slot : m_Guards)
			{
				slot.alive = true;
				slot.bodyFound = false;
				if (SceneObject* o = SceneObjectAt(slot.sceneIndex)) o->visible = true;
			}

			BuildLevel(m_Character->GetPosition(), m_Character->GetForward());
		}

		// How lit the player is, in [kMinExposure, 1]. 0-ish = deep shadow
		// (hard to see), 1 = standing in firelight (fully exposed).
		//
		// Two contributions, both resolved with object-only raycasts (terrain is
		// excluded, so a grazing hillside can't false-shadow you):
		//  - Moonlight: cast toward the sky along the moon's reverse direction.
		//    Unobstructed = moonlit; a wall or roof in the way = in its shadow.
		//  - Braziers: each point light within radius and with a clear line adds
		//    firelight by distance falloff.
		float ComputeExposure(const glm::vec3& eye) const
		{
			float exposure = 0.0f;

			// Moonlight.
			for (const Light& light : m_Scene->GetLights())
			{
				if (!light.enabled || light.type != LightType::Directional) continue;

				glm::vec3 toSky = -light.direction;
				if (glm::dot(toSky, toSky) < 1e-6f) continue;
				toSky = glm::normalize(toSky);

				Ray ray(eye, toSky);
				if (!m_Scene->Raycast(ray, 60.0f, m_BodyIndex).hit)
					exposure += kMoonExposure;
				break;   // one moon
			}

			// Braziers.
			for (const Light& light : m_Scene->GetLights())
			{
				if (!light.enabled || light.type != LightType::Point) continue;

				glm::vec3 toLight = light.position - eye;
				float dist = glm::length(toLight);
				if (dist >= light.radius || dist < 1e-4f) continue;

				// Occluded by geometry? Object-only ray; braziers themselves
				// don't block sight, so they can't self-shadow.
				Ray ray(eye, toLight / dist);
				RayHit hit = m_Scene->Raycast(ray, dist - 0.1f, m_BodyIndex);
				if (hit.hit) continue;

				exposure += 1.0f - (dist / light.radius);   // full up close, 0 at the edge
			}

			return glm::clamp(exposure, kMinExposure, 1.0f);
		}

		// Concealment from grass, in (0, 1]: 1 = no cover, lower = better hidden.
		// Scales hard with how LOW you are, so grass is the payoff for crawling —
		// standing in it barely helps, prone in it makes you nearly unseeable.
		// `stanceValue` is [0,2] as CharacterPawn reports it. Passed explicitly so
		// the same grass can be queried for a BODY (always prone = 2.0) as well
		// as for the player — otherwise grass hides you but not the corpse you
		// left in it, which reads as a bug and removes an obvious tactic.
		float ConcealmentAt(const glm::vec3& feet, float stanceValue) const
		{
			for (const auto& v : m_ConcealVolumes)
			{
				if (std::fabs(feet.x - v.center.x) <= v.halfExtents.x &&
					std::fabs(feet.z - v.center.y) <= v.halfExtents.y)
				{
					return StanceMix(stanceValue, kGrassStand, kGrassCrouch, kGrassProne);
				}
			}
			return 1.0f;
		}

		float ComputeConcealment(const glm::vec3& feet) const
		{
			return ConcealmentAt(feet, m_Character->GetStanceValue());
		}

		//----------------------------------------------------------------------
		// Mission state
		//----------------------------------------------------------------------

		bool MissionOver() const
		{
			return m_Mission == Mission::Escaped || m_Mission == Mission::Caught;
		}

		bool CarryingRelic() const { return m_Mission == Mission::Extract; }

		// Nearest living guard the player could stealth-kill right now, or -1.
		//
		// Requires being BEHIND them: the whole point is that it's a positional
		// problem, not a proximity one. A guard who has already spotted you
		// can't be taken down — otherwise detection would carry no risk, since
		// walking up to whoever made you would always be the answer.
		int FindTakedownTarget() const
		{
			if (MissionOver() || IsRiding()) return -1;

			const glm::vec3 playerPos = m_Character->GetPosition();
			int   best = -1;
			float bestDistSq = kTakedownRange * kTakedownRange;

			for (size_t i = 0; i < m_Guards.size(); ++i)
			{
				const GuardSlot& slot = m_Guards[i];
				if (!slot.alive) continue;

				// A guard who has NOTICED you can't be silently taken down.
				// (Previously only fully-Alerted guards were excluded, which let
				// you sit in cover and farm the whole camp as they walked up.)
				// Investigating is deliberately still fair game: they're walking
				// toward a noise, not at *you* — that's the distraction-into-
				// takedown combo, and it's the good version of this move.
				const AlertState state = slot.agent.GetAlertState();
				if (state == AlertState::Alerted || state == AlertState::Suspicious) continue;

				glm::vec3 toPlayer = playerPos - slot.agent.GetPosition();
				toPlayer.y = 0.0f;

				float distSq = glm::dot(toPlayer, toPlayer);
				if (distSq > bestDistSq) continue;
				if (!slot.agent.IsBehind(playerPos)) continue;

				best = static_cast<int>(i);
				bestDistSq = distSq;
			}

			return best;
		}

		bool CanTakeRelic() const
		{
			if (m_Mission != Mission::Infiltrate) return false;
			glm::vec3 d = m_Character->GetPosition() - m_RelicHome;
			d.y = 0.0f;
			return glm::dot(d, d) <= kRelicPickupRange * kRelicPickupRange;
		}

		bool CanExtract() const
		{
			if (m_Mission != Mission::Extract) return false;
			glm::vec3 d = m_Character->GetPosition() - m_ExtractPoint;
			d.y = 0.0f;
			return glm::dot(d, d) <= kExtractRange * kExtractRange;
		}

		void PerformTakedown(int guardIndex)
		{
			if (guardIndex < 0 || guardIndex >= static_cast<int>(m_Guards.size())) return;

			GuardSlot& slot = m_Guards[guardIndex];
			slot.alive = false;
			slot.bodyFound = false;
			// The body stays exactly where they fell. Leaving it in the middle
			// of a patrol route is a mistake you get to make.
			slot.bodyPosition = slot.agent.GetPosition();
		}

		// Throw a stone at whatever you're looking at. The verb that turns
		// waiting into planning: you can pull a guard off their route instead
		// of watching for a gap in it.
		// The launch state for a throw from where the player is looking. Shared
		// by the preview and the actual throw so the two cannot disagree — a
		// preview that lies is worse than no preview.
		bool ComputeThrow(glm::vec3& outPos, glm::vec3& outVel) const
		{
			const glm::vec3 eye = m_Character->GetCameraFocus();
			glm::vec3 dir = m_Camera->GetForward();
			if (glm::dot(dir, dir) < 1e-6f) return false;
			dir = glm::normalize(dir);

			outPos = eye + dir * 0.6f;
			// Bias upward so a flat look still lobs rather than drilling into
			// the floor two metres ahead.
			outVel = (dir + glm::vec3(0.0f, 0.25f, 0.0f)) * kStoneSpeed;
			return true;
		}

		void ThrowStone()
		{
			if (m_StonesLeft <= 0 || m_Stone.active || MissionOver()) return;

			glm::vec3 pos, vel;
			if (!ComputeThrow(pos, vel)) return;

			m_Stone.active = true;
			m_Stone.position = pos;
			m_Stone.velocity = vel;
			--m_StonesLeft;
		}

		// Ballistic arc until it hits the ground, then a noise event.
		void UpdateStone(float dt)
		{
			if (!m_Stone.active) return;

			m_Stone.velocity.y += kStoneGravity * dt;
			m_Stone.position += m_Stone.velocity * dt;

			const float groundY = m_GroundFn(m_Stone.position.x, m_Stone.position.z);
			if (m_Stone.position.y > groundY)
				return;

			// Landed: everyone within earshot goes to look. Distance-gated only
			// — sound goes around corners, which is exactly why a stone can
			// reach a guard your sight lines can't.
			const glm::vec3 impact(m_Stone.position.x, groundY, m_Stone.position.z);
			m_Stone.active = false;

			for (auto& slot : m_Guards)
			{
				if (!slot.alive) continue;
				glm::vec3 d = slot.agent.GetPosition() - impact;
				d.y = 0.0f;
				if (glm::dot(d, d) <= kStoneNoiseRadius * kStoneNoiseRadius)
					slot.agent.InvestigatePoint(impact);
			}
		}

		// Move each bearer's torch to their hand, and keep unlit / dead guards'
		// torches off. A carried torch is a real point light: it lights the
		// player near the guard (via ComputeExposure, which reads all lights),
		// so brushing past a torch-bearer in the dark lights you up.
		void UpdateTorches()
		{
			for (const auto& slot : m_Guards)
			{
				const bool lit = slot.alive && slot.carriesTorch;

				if (slot.torchLightIndex < m_Scene->GetLightCount())
				{
					Light* light = m_Scene->GetLight(slot.torchLightIndex);
					light->enabled = lit;
					if (lit)
						// Held out ahead and up, roughly at a raised-arm height.
						light->position = slot.agent.GetPosition()
							+ slot.agent.GetForward() * 0.5f + glm::vec3(0.0f, 1.8f, 0.0f);
				}

				if (SceneObject* prop = SceneObjectAt(slot.torchPropIndex))
				{
					prop->visible = lit;
					if (lit && prop->meshDrawable)
					{
						glm::vec3 p = slot.agent.GetPosition()
							+ slot.agent.GetForward() * 0.5f + glm::vec3(0.0f, 1.5f, 0.0f);
						prop->meshDrawable->SetTransform(
							glm::translate(glm::mat4(1.0f), p) *
							glm::scale(glm::mat4(1.0f), glm::vec3(0.15f, 0.6f, 0.15f)));
						prop->meshDrawable->SetCustomData(glm::vec4(1.0f, 0.6f, 0.2f, 1.0f));
					}
				}
			}
		}

		// Periodic GPU cost breakdown by render pass. The frame is ~90% GPU-bound,
		// so this is where the answer to "what killed my framerate" lives — the
		// CPU-side guard/raycast cost is already on the detection HUD and is
		// microseconds. Logged rather than drawn so it's greppable from a run.
		void LogGpuBreakdown(float dt)
		{
			m_GpuLogTimer += dt;
			if (m_GpuLogTimer < 5.0f) return;
			m_GpuLogTimer = 0.0f;

			GpuProfiler* prof = GetRenderer()->GetGpuProfiler();
			if (!prof || !prof->IsSupported()) return;

			std::string line;
			float total = 0.0f;
			for (const auto& r : prof->GetResults())
			{
				line += "  " + r.name + " " + std::to_string(r.ms).substr(0, 5) + "ms";
				total += r.ms;
			}
			LOG_INFO("=== GPU breakdown ==={} | total {:.2f}ms", line, total);
		}

		// Hand torches to more guards — the "camp lights up when alerted" hook.
		// Wired for the Phase 1c alert system; harmless to call now. Lights the
		// darkest (torchless) living guards first so the newly-lit areas are the
		// ones that were previously safe.
		void LightMoreGuards(int howMany)
		{
			for (auto& slot : m_Guards)
			{
				if (howMany <= 0) break;
				if (slot.alive && !slot.carriesTorch) { slot.carriesTorch = true; --howMany; }
			}
		}

		// Guards find bodies. This is what gives a takedown a cost: kill someone
		// in the open and the next patrol through will find them, investigate,
		// and be primed and dangerous when they get there.
		void UpdateBodyDiscovery(float dt)
		{
			// Throttled: this is O(bodies x guards) line-of-sight queries, and
			// with 8 guards that's up to 64 per frame for a check that nobody
			// can perceive the latency of. A fifth of a second is far below the
			// reaction time this is modelling.
			m_BodyScanTimer -= dt;
			if (m_BodyScanTimer > 0.0f) return;
			m_BodyScanTimer = 0.2f;

			for (auto& corpse : m_Guards)
			{
				if (corpse.alive || corpse.bodyFound) continue;

				for (auto& observer : m_Guards)
				{
					if (!observer.alive) continue;

					glm::vec3 d = observer.agent.GetPosition() - corpse.bodyPosition;
					d.y = 0.0f;
					if (glm::dot(d, d) > kBodyNoticeRange * kBodyNoticeRange) continue;

					// A body lies flat, so it's sighted low AND gets the same
					// concealment a prone player would — a corpse dumped in tall
					// grass is genuinely hidden, which makes *where* you kill
					// someone a real decision.
					const float bodyConceal = ConcealmentAt(corpse.bodyPosition, 2.0f);
					if (!observer.agent.CanSeePoint(corpse.bodyPosition, 0.3f, bodyConceal))
						continue;

					corpse.bodyFound = true;

					// Whoever finds it converges, and the rest of the camp gets
					// uneasy — a found body should change the whole run, not
					// just one guard's afternoon.
					for (auto& other : m_Guards)
						if (other.alive)
							other.agent.InvestigatePoint(corpse.bodyPosition);
					break;
				}
			}
		}

		// Advances the objective. Kept separate from input so the win/lose rules
		// live in one place rather than being scattered through the interact
		// handler.
		void UpdateMission(float dt)
		{
			if (MissionOver()) return;

			m_RelicSpin += dt;

			// Caught the moment any living guard fully confirms you. Harsh by
			// design: a stealth game where being seen is survivable is a combat
			// game, and there is no combat here yet.
			for (const auto& slot : m_Guards)
			{
				if (slot.alive && slot.agent.GetAlertState() == AlertState::Alerted)
				{
					m_Mission = Mission::Caught;
					return;
				}
			}

			if (CanExtract())
				m_Mission = Mission::Escaped;
		}

		// Full reset — used by the restart key. Rebuilds the camp from the
		// player's spawn so a retry is identical to the first attempt.
		void RestartMission()
		{
			m_Character->SetPosition(glm::vec3(m_ExtractPoint.x,
				m_GroundFn(m_ExtractPoint.x, m_ExtractPoint.z), m_ExtractPoint.z));
			Possess(m_Character.get());

			for (auto& slot : m_Guards)
			{
				slot.alive = true;
				slot.bodyFound = false;
				if (SceneObject* o = SceneObjectAt(slot.sceneIndex))
					o->visible = true;
			}

			BuildLevel(m_Character->GetPosition(), m_Character->GetForward());
		}

		// One interact key, resolved by priority. Takedown outranks everything
		// because it's the time-critical one — you're standing behind someone
		// who is about to turn around, and having it lose to a relic prompt you
		// happen to be overlapping would be maddening.
		void Interact()
		{
			if (MissionOver()) return;

			if (int target = FindTakedownTarget(); target >= 0)
			{
				PerformTakedown(target);
				return;
			}

			if (CanTakeRelic())
			{
				m_Mission = Mission::Extract;
				if (SceneObject* o = SceneObjectAt(m_RelicIndex))
					o->visible = false;      // it's on your back now
				return;
			}

			ToggleRide();
		}

		// One key (Ctrl) drives all three stances:
		//   tap  = toggle crouch (stand<->crouch, or up one level from prone)
		//   hold = drop to prone
		// Tap vs hold is resolved on the timer: a short press toggles on
		// RELEASE, a long press drops prone the moment it crosses the threshold.
		// Persistent request, so stance survives a jump and isn't re-held every
		// frame — which is what "make crouch a toggle" asked for.
		void UpdateStanceInput(float dt, PawnInput& in)
		{
			const bool ctrlDown = GetInput()->IsDown(InputCode::Key_Control);
			const float kProneHoldTime = 0.22f;   // hold past this = prone

			if (ctrlDown && !m_CtrlWasDown)        // press
			{
				m_CtrlHoldTime = 0.0f;
				m_CtrlHoldConsumed = false;
			}
			else if (ctrlDown)                     // holding
			{
				m_CtrlHoldTime += dt;
				if (m_CtrlHoldTime >= kProneHoldTime && !m_CtrlHoldConsumed)
				{
					m_StanceRequest = CharacterPawn::Stance::Prone;
					m_CtrlHoldConsumed = true;      // the hold has done its job
				}
			}
			else if (!ctrlDown && m_CtrlWasDown)   // release
			{
				if (!m_CtrlHoldConsumed)            // it was a tap
				{
					// Stand -> crouch; crouch -> stand; prone -> crouch (up one).
					m_StanceRequest = (m_StanceRequest == CharacterPawn::Stance::Standing)
						? CharacterPawn::Stance::Crouching
						: (m_StanceRequest == CharacterPawn::Stance::Prone)
							? CharacterPawn::Stance::Crouching
							: CharacterPawn::Stance::Standing;
				}
			}
			m_CtrlWasDown = ctrlDown;

			in.crouch = (m_StanceRequest == CharacterPawn::Stance::Crouching);
			in.prone  = (m_StanceRequest == CharacterPawn::Stance::Prone);
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
					// Squash the box by the stance blend so crouching is visible
					// from the outside, not just felt through the camera. Feet
					// stay planted: the box shrinks downward, so half-height
					// tracks the scale rather than staying at the standing value.
					const float stance = m_Character->GetHeightFraction();
					const glm::vec3 bodyScale(0.6f, 1.8f * stance, 0.4f);
					const float halfHeight = 0.9f * stance;

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

			// Guards: same person-sized box, tinted by alert state. The tint IS
			// the readout — you can tell whether you've been noticed by looking
			// at the guard rather than at a HUD number, which is how it should
			// work once there's a real character with an animation to sell it.
			for (const auto& slot : m_Guards)
			{
				SceneObject* guardObj = SceneObjectAt(slot.sceneIndex);
				if (!guardObj || !guardObj->meshDrawable) continue;

				if (slot.alive)
				{
					const glm::vec3 guardScale(0.6f, 1.8f, 0.4f);
					guardObj->meshDrawable->SetTransform(
						slot.agent.GetVisualTransform()
						* glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.9f, 0.0f))
						* glm::scale(glm::mat4(1.0f), guardScale));

					// Mesh.frag reads customData.w > 0.01 as "use this tint",
					// xyz = colour. Alpha 1 keeps it opaque on the Mesh pipeline.
					guardObj->meshDrawable->SetCustomData(
						glm::vec4(slot.agent.GetDebugTint(), 1.0f));
				}
				else
				{
					// Body: the same box laid flat where they fell. It stays
					// visible and stays findable — that's the whole point.
					// Dark red once discovered, so you can see which of your
					// mistakes has already been noticed.
					guardObj->meshDrawable->SetTransform(
						glm::translate(glm::mat4(1.0f), slot.bodyPosition + glm::vec3(0.0f, 0.2f, 0.0f))
						* glm::scale(glm::mat4(1.0f), glm::vec3(1.8f, 0.4f, 0.6f)));

					guardObj->meshDrawable->SetCustomData(slot.bodyFound
						? glm::vec4(0.55f, 0.05f, 0.05f, 1.0f)
						: glm::vec4(0.30f, 0.16f, 0.16f, 1.0f));
				}
			}

			// Stone in flight.
			if (SceneObject* stoneObj = SceneObjectAt(m_StoneIndex))
			{
				stoneObj->visible = m_Stone.active;
				if (m_Stone.active && stoneObj->meshDrawable)
				{
					stoneObj->meshDrawable->SetTransform(
						glm::translate(glm::mat4(1.0f), m_Stone.position)
						* glm::scale(glm::mat4(1.0f), glm::vec3(0.18f)));
					stoneObj->meshDrawable->SetCustomData(glm::vec4(0.7f, 0.7f, 0.75f, 1.0f));
				}
			}

			// Relic: a small spinning gold box, bobbing so it reads as a pickup
			// rather than scenery. Hidden once carried.
			if (SceneObject* relicObj = SceneObjectAt(m_RelicIndex))
			{
				if (relicObj->meshDrawable && !CarryingRelic())
				{
					glm::vec3 pos = m_RelicHome;
					pos.y += 0.7f + std::sin(m_RelicSpin * 2.0f) * 0.12f;

					relicObj->meshDrawable->SetTransform(
						glm::translate(glm::mat4(1.0f), pos)
						* glm::rotate(glm::mat4(1.0f), m_RelicSpin, glm::vec3(0.0f, 1.0f, 0.0f))
						* glm::scale(glm::mat4(1.0f), glm::vec3(0.5f)));

					relicObj->meshDrawable->SetCustomData(glm::vec4(1.0f, 0.85f, 0.25f, 1.0f));
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

		// Overrides the loaded scene's lighting to a night look: dark cool
		// ambient + a dim, cool "moon" directional. The warm braziers then
		// become the dominant light, which is what makes shadow a hiding place.
		void SetupNightLighting()
		{
			// Deep, slightly blue night ambient. Low enough that unlit areas are
			// genuinely dark, high enough you can still navigate by moonlight.
			m_Scene->SetAmbient(glm::vec3(0.05f, 0.06f, 0.11f), 0.35f);

			// Find the primary directional light (the moon) or make one. Cool,
			// dim, angled — moonlight, not a sun.
			Light* moon = nullptr;
			for (auto& light : m_Scene->GetLights())
				if (light.type == LightType::Directional) { moon = &light; break; }
			if (!moon)
				moon = m_Scene->AddLight("Moon", LightType::Directional);

			moon->direction = glm::normalize(glm::vec3(0.35f, -0.82f, 0.45f));
			moon->color     = glm::vec3(0.45f, 0.52f, 0.72f);   // pale cool blue
			moon->intensity = 0.35f;
			moon->enabled   = true;
			moon->shadowConfig.castsShadows = true;

			GetRenderer()->SetShadowConfig(moon->shadowConfig);
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

		//--------------------------------------------------------------------------
		// The camp (ROADMAP 3.4) — one hand-built encampment with an objective.
		//--------------------------------------------------------------------------

		// Infiltrate -> steal the relic -> carry it back out. Two-way, so the trip
		// in is cautious and the trip out is tense: the guards haven't moved but
		// you now know where they are and you can't afford a mistake.
		enum class Mission { Infiltrate, Extract, Escaped, Caught };

		struct GuardSlot
		{
			GuardAgent agent;
			int        sceneIndex = -1;
			bool       alive = true;

			// Where the body fell. Bodies PERSIST: a takedown that erases all
			// evidence makes killing free, which is what made the first pass
			// feel consequence-less. Now every takedown leaves a thing that can
			// be found, so *where* you do it is a decision.
			glm::vec3  bodyPosition{ 0.0f };
			bool       bodyFound = false;

			// Torch: a moving point light this guard carries. Only SOME guards
			// carry one (entrance + a few) — a torch-bearer lights up whatever's
			// near them (so hugging them is dangerous) and is easy to track in
			// the dark. Allocated once; enabled per build / on alert.
			bool       carriesTorch = false;
			int        torchPropIndex = -1;
			size_t     torchLightIndex = 0;
		};

		std::vector<GuardSlot> m_Guards;
		std::vector<int>       m_StructureIndices;   // walls + crates
		int   m_RelicIndex = -1;
		float m_RelicSpin = 0.0f;                    // so it reads as a pickup

		Mission   m_Mission = Mission::Infiltrate;
		glm::vec3 m_RelicHome{ 0.0f };               // where the relic sits in the camp
		glm::vec3 m_ExtractPoint{ 0.0f };            // where you must return to
		bool      m_CampBuilt = false;

		// The direction the camp was laid out along, captured at first spawn.
		// Restart must reuse it: rebuilding from wherever the player happens to
		// be facing would relocate the whole camp on every retry.
		glm::vec3 m_CampForward{ 1.0f, 0.0f, 0.0f };

		static constexpr int   kGuardCount = 8;
		static constexpr float kRelicPickupRange = 2.0f;
		static constexpr float kExtractRange = 4.0f;

		// Takedown: close, behind them, and they haven't already made you.
		static constexpr float kTakedownRange = 2.2f;

		// --- Distraction ------------------------------------------------------
		// A throwable stone. THE answer to "all I can do is wait": it lets the
		// player manufacture an opening rather than watch for one. Guards near
		// where it lands go and look, which also gives the investigation
		// behaviour something to react to besides bodies.
		struct Stone
		{
			bool      active = false;
			glm::vec3 position{ 0.0f };
			glm::vec3 velocity{ 0.0f };
		};
		Stone m_Stone;
		int   m_StoneIndex = -1;
		int   m_StonesLeft = 0;
		bool  m_AimingStone = false;   // holding Q
		bool  m_TrackingHeld = false;  // holding F

		static constexpr int   kStoneCount = 5;      // limited, so throws are decisions
		static constexpr float kStoneSpeed = 16.0f;
		static constexpr float kStoneGravity = -20.0f;
		static constexpr float kStoneNoiseRadius = 14.0f;

		// How close a guard must be to notice a body, and how far it can see one.
		static constexpr float kBodyNoticeRange = 12.0f;
		float m_BodyScanTimer = 0.0f;   // throttles the body-discovery sweep

		// --- Light & shadow concealment (Phase 1a) ----------------------------
		// Shadow is a hiding mechanic: standing in the dark shortens how far and
		// how fast guards perceive you. Computed CPU-side from the scene lights
		// via raycasts (reuses Scene::Raycast) — no new engine rendering work.
		static constexpr int   kBrazierCount = 6;
		static constexpr float kBrazierRadius = 9.0f;
		static constexpr float kTorchRadius   = 7.0f;
		static constexpr float kMoonExposure  = 0.7f;   // open moonlight is fairly exposing
		static constexpr float kMinExposure   = 0.12f;  // pitch dark isn't literal invisibility
		static constexpr float kShadowRateFloor  = 0.35f; // detection RATE in full shadow
		static constexpr float kShadowRangeFloor = 0.30f; // sight RANGE in full shadow

		int    m_BrazierPropIndices[kBrazierCount] = { -1, -1, -1, -1, -1, -1 };
		size_t m_BrazierLightIndices[kBrazierCount] = { 0, 0, 0, 0, 0, 0 };
		float  m_PlayerExposure = 1.0f;   // last computed, for the HUD

		// --- Concealment volumes: tall grass (the reason to crawl) ------------
		// Horizontal regions where being LOW hides you: standing in grass barely
		// helps, crouching helps, prone makes you nearly invisible. Turns prone
		// from "slow crouch" into "cross the open field unseen". Grass neither
		// blocks sight nor movement — it conceals only via this explicit check.
		static constexpr int kGrassCount = 7;
		// Softened after play-test: 0.07 prone made crawling strictly dominant —
		// there was no reason to ever do anything else. Grass should be a strong
		// option, not the only one. It also stacks multiplicatively with stance
		// and shadow, so small numbers here compound hard.
		static constexpr float kGrassStand  = 0.8f;   // exposure mult, standing in grass
		static constexpr float kGrassCrouch = 0.5f;
		static constexpr float kGrassProne  = 0.22f;  // strong, not absolute

		struct ConcealVolume { glm::vec2 center; glm::vec2 halfExtents; };
		std::vector<ConcealVolume> m_ConcealVolumes;
		int   m_GrassPropIndices[kGrassCount] = { -1, -1, -1, -1, -1, -1, -1 };
		float m_PlayerConcealment = 1.0f;   // last computed, for the HUD
		float m_GpuLogTimer = 0.0f;         // periodic GPU breakdown log
		float m_PlayerNoise = 0.0f;         // last computed, for the HUD

		// Perception cost tracking — shown on the detection HUD. Temporary
		// diagnostic for the reported frame-rate drop; remove once resolved.
		float m_GuardMsAccum = 0.0f;
		float m_GuardMsPeak = 0.0f;
		int   m_GuardSamples = 0;
		float m_WorstFrameMs = 0.0f;

		// How close you must be, on foot, to mount the bike.
		static constexpr float kMountRadius = 3.5f;

		// Stance stealth profile. Two levers per stance:
		//   *Visibility — detection RATE (how fast the meter fills).
		//   *RangeMul   — effective sight RANGE (how far you register).
		// Standing keeps full range so guards spot a standing figure from
		// distance; crouch and especially prone pull both down hard.
		static constexpr float kCrouchVisibility = 0.5f;
		static constexpr float kProneVisibility  = 0.25f;
		static constexpr float kCrouchRangeMul   = 0.7f;
		static constexpr float kProneRangeMul    = 0.45f;

		// Noise per stance (multiplied by actual speed — see ComputeNoise).
		// Tuned so the loudness order at each stance's own pace is:
		//   sprint (~1.0, LOUD)  >  walk (~0.5)  >  crawl (~0.35)  >  crouch (~0.2)
		// Walking is deliberately only moderate now — sprinting is what's loud.
		// Prone still rustles more per metre than crouching (crawling is noisy),
		// so it lands above crouch-walk but below a normal walk.
		static constexpr float kNoiseStanding     = 0.5f;   // walk moderate, sprint (2x speed) loud
		static constexpr float kNoiseCrouched     = 0.45f;
		static constexpr float kNoiseProne        = 1.6f;   // high per-metre, but prone is slow
		static constexpr float kNoiseGrassSurface = 1.4f;   // grass rustles

		// Stance input state (tap = crouch toggle, hold = prone).
		CharacterPawn::Stance m_StanceRequest = CharacterPawn::Stance::Standing;
		bool  m_CtrlWasDown = false;
		float m_CtrlHoldTime = 0.0f;
		bool  m_CtrlHoldConsumed = false;

		// Piecewise stance lerp, matching CharacterPawn's stance value in [0,2]:
		// stand at 0, crouch at 1, prone at 2.
		static float StanceMix(float v, float stand, float crouch, float prone)
		{
			v = std::clamp(v, 0.0f, 2.0f);
			return (v < 1.0f) ? glm::mix(stand, crouch, v)
			                  : glm::mix(crouch, prone, v - 1.0f);
		}

		// How much NOISE the player is making, [0,1].
		//
		// noise = stance factor x how fast you're actually moving x surface.
		//
		// The stance factors are the interesting part. Crawling is given a HIGH
		// factor (2.5) even though prone is the slowest stance — dragging
		// yourself along rustles. The result is that prone lands *louder than a
		// crouch-walk but quieter than walking*, which is deliberate: prone is
		// the strongest stance visually, so without an audible cost it's simply
		// dominant (which is exactly what the grass play-test exposed). Now
		// crawling trades "hard to see" against "easy to hear", and crouch-walk
		// becomes the genuinely stealthy-but-slow middle option.
		float ComputeNoise() const
		{
			const float speed = m_Character->GetHorizontalSpeed();
			if (speed < 0.25f) return 0.0f;                 // standing still is silent

			const float walk = std::max(m_Character->GetConfig().walkSpeed, 1e-3f);
			const float normalizedSpeed = speed / walk;      // 1.0 = a normal walk

			const float stanceFactor = StanceMix(m_Character->GetStanceValue(),
				kNoiseStanding, kNoiseCrouched, kNoiseProne);

			// Surface: grass rustles. Reuses the concealment volumes, so the
			// cover that hides you is also the cover that gives you away.
			const bool inGrass = (ConcealmentAt(m_Character->GetPosition(), 0.0f) < 1.0f);
			const float surface = inGrass ? kNoiseGrassSurface : 1.0f;

			return glm::clamp(normalizedSpeed * stanceFactor * surface, 0.0f, 1.0f);
		}

		// How conspicuous the player's MOTION is, [0,1]. Standing still = 0; a
		// full sprint = 1. This is what lets a running figure be caught at range
		// where a still one is missed — motion catches the eye.
		float ComputeMotion() const
		{
			const float speed = m_Character->GetHorizontalSpeed();
			const float sprintSpeed = m_Character->GetConfig().walkSpeed
				* m_Character->GetConfig().sprintMul;
			const float deadzone = 0.3f;   // ignore residual drift
			return glm::clamp((speed - deadzone) / std::max(sprintSpeed - deadzone, 1e-3f),
				0.0f, 1.0f);
		}

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
