//------------------------------------------------------------------------------
// EditorApp.cpp
//
// Nightbloom Editor Application
// Orchestrates panels and engine lifecycle. Panels own their own state.
//------------------------------------------------------------------------------

#include "Engine/Core/Application.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include "Engine/Renderer/PipelineInterface.hpp"
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include "Engine/Renderer/Camera.hpp"
#include "Engine/Renderer/AssetManager.hpp"
#include "Engine/Renderer/GLTFLoader.hpp"
#include "Engine/Renderer/Model.hpp"
#include "Engine/Renderer/Material.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Core/Scene.hpp"
#include "Engine/Core/SceneSerializer.hpp"
#include "Engine/Core/Project.hpp"
#include "EditorFileUtils.hpp"
#include "EditorContext.hpp"

// Panels
#include "Panels/ConsolePanel.hpp"
#include "Panels/AssetBrowserPanel.hpp"
#include "Panels/ViewportPanel.hpp"
#include "Panels/DebugPanel.hpp"
#include "Panels/SceneHierarchyPanel.hpp"
#include "Panels/InspectorPanel.hpp"
#include "Panels/LightingPanel.hpp"
#include "Panels/DayNightPanel.hpp"
#include "Panels/ProjectSettingsPanel.hpp"
#include "Panels/NoiseDebugPanel.hpp"
#include "Panels/ShaderCompilerPanel.hpp"
#include "Panels/LiveShaderTestPanel.hpp"
#include "Panels/TerrainEditorPanel.hpp"
#include "Panels/GrassPanel.hpp"
#include "Panels/FireflyPanel.hpp"
#include "Panels/CloudPanel.hpp"
#include "Panels/WaterEditorPanel.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <algorithm>

// Check if docking is available
#ifdef IMGUI_HAS_DOCK
#define USE_IMGUI_DOCKING 1
#else
#define USE_IMGUI_DOCKING 0
#endif

namespace Nightbloom {

    class EditorApplication : public Application
    {
    public:
        EditorApplication() : Application("Nightbloom Editor")
        {
            LOG_INFO("=== Nightbloom Editor Starting ===");
            SetupDefaultProject();
        }

        ~EditorApplication()
        {
            if (GetRenderer())
                GetRenderer()->WaitForIdle();

            // Cleanup panels that hold GPU resources before renderer goes away
            m_NoiseDebug.Cleanup();
            m_TerrainPanel.Cleanup();
            m_GrassPanel.Cleanup();
            m_FireflyPanel.Cleanup();
            m_CloudPanel.Cleanup();
            m_WaterPanel.Cleanup();

            m_EditorScene.reset();
            SaveEditorSettings();

            LOG_INFO("=== Nightbloom Editor Shutdown Complete ===");
        }

        void OnStartup() override
        {
            LOG_INFO("Editor application started");

            if (GetWindow())
            {
                UpdateWindowTitle();
            }

            // Camera
            m_Camera = std::make_unique<Camera>();
            m_Camera->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
            m_Camera->SetRotation(-135.0f, -20.0f);
            m_Camera->SetPerspectiveInfiniteReverseZ(45.0f, 1280.0f / 720.0f, 0.1f);
            GetRenderer()->SetCameraPosition(m_Camera->GetPosition());
            GetRenderer()->SetViewMatrix(m_Camera->GetViewMatrix());
            GetRenderer()->SetProjectionMatrix(m_Camera->GetProjectionMatrix());

            // Scene (populated below by BootProject — either a loaded project scene
            // or the seeded Sample project).
            m_EditorScene = std::make_unique<Scene>();

            // ImGui config
#if USE_IMGUI_DOCKING
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif

            // Initialize panels
            m_ShaderCompiler.Initialize();

            // Window resize -> update camera aspect
            GetWindow()->SetResizeCallback([this](uint32_t width, uint32_t height) {
                if (width > 0 && height > 0 && m_Camera)
                {
                    float aspect = static_cast<float>(width) / static_cast<float>(height);
                    m_Camera->SetPerspectiveInfiniteReverseZ(45.0f, aspect, 0.1f);
                }
                });

            // Boot into a project: last-opened if any, else the bundled Sample.
            BootProject();
        }

        // Build the engine's default demo scene (ToyCar + test cubes + Moonlight +
        // emissive moon) into the current, empty m_EditorScene. Used to seed the
        // bundled Sample project the first time the editor runs. Requires an
        // initialized Renderer (loads a model + uses built-in geometry buffers).
        void BuildDefaultScene()
        {
            ResourceManager* resources = GetRenderer()->GetResourceManager();
            Texture* defaultTex = resources ? resources->GetTexture("default_white") : nullptr;

            // Load ToyCar
            auto toyCar = std::make_unique<Model>("ToyCar");
            std::string modelPath = AssetManager::Get().GetModelPath("ToyCar/ToyCar.gltf");

            if (toyCar->LoadFromFile(modelPath,
                GetRenderer()->GetResourceManager(),
                GetRenderer()->GetDescriptorManager()))
            {
                toyCar->SetScale(.012f);
                toyCar->SetPosition(glm::vec3(-3.0f, -2.0f, -3.0f));
                m_EditorScene->AddObject("ToyCar", std::move(toyCar), defaultTex);
                LOG_INFO("Added ToyCar to scene");
            }

            // Test cubes
            auto* vertexBuffer = GetRenderer()->GetTestVertexBuffer();
            auto* indexBuffer = GetRenderer()->GetTestIndexBuffer();
            uint32_t indexCount = GetRenderer()->GetTestIndexCount();

            if (vertexBuffer && indexBuffer && indexCount > 0)
            {
                auto cube1 = std::make_unique<MeshDrawable>(
                    vertexBuffer, indexBuffer, indexCount, PipelineType::Mesh);
                if (auto* checker = resources->GetTexture("uv_checker"))
                    cube1->AddTexture(checker);
                SceneObject* obj1 = m_EditorScene->AddPrimitive("TestCube1", std::move(cube1));
                obj1->textureIndex = 0;
                obj1->pipeline = PipelineType::Mesh;
                obj1->primitiveKind = PrimitiveKind::TestCube;
                obj1->primitiveTexture = "uv_checker";

                auto cube2 = std::make_unique<MeshDrawable>(
                    vertexBuffer, indexBuffer, indexCount, PipelineType::Mesh);
                if (auto* white = resources->GetTexture("default_white"))
                    cube2->AddTexture(white);
                SceneObject* obj2 = m_EditorScene->AddPrimitive("TestCube2", std::move(cube2));
                obj2->textureIndex = 1;
                obj2->pipeline = PipelineType::Mesh;
                obj2->primitiveKind = PrimitiveKind::TestCube;
                obj2->primitiveTexture = "default_white";
                obj2->primitivePosition = glm::vec3(0.5f, 0.2f, -1.0f);
                obj2->UpdatePrimitiveTransform();
            }

            // Ground plane
            // auto* gpVB = GetRenderer()->GetGroundPlaneVertexBuffer();
            // auto* gpIB = GetRenderer()->GetGroundPlaneIndexBuffer();
            // uint32_t gpIC = GetRenderer()->GetGroundPlaneIndexCount();
            // 
            // if (gpVB && gpIB && gpIC > 0)
            // {
            //     auto groundPlane = std::make_unique<MeshDrawable>(
            //         gpVB, gpIB, gpIC, PipelineType::Mesh);
            //     glm::mat4 groundTransform =
            //         glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.5f, 0.0f));
            //     groundPlane->SetTransform(groundTransform);
            //     if (auto* white = resources->GetTexture("default_white"))
            //         groundPlane->AddTexture(white);
            //     SceneObject* groundObj =
            //         m_EditorScene->AddPrimitive("Ground", std::move(groundPlane));
            //     groundObj->textureIndex = 1;
            //     groundObj->pipeline = PipelineType::Mesh;
            //     groundObj->primitiveTransform = groundTransform;
            // }

            // Lighting
            Light* moonlight = m_EditorScene->AddLight("Moonlight", LightType::Directional);
            moonlight->direction = glm::vec3(-0.3f, -0.8f, -0.5f);
            moonlight->color = glm::vec3(0.6f, 0.7f, 1.0f);
            moonlight->intensity = 0.8f;

            moonlight->shadowConfig.castsShadows = true;
            // CSM (frustum-fit): cascades are auto-sized to the camera view frustum, so
            // orthoSize/near/far no longer apply. The two knobs that matter:
            //   shadowDistance - how far shadows reach (outermost cascade far)
            //   splitLambda    - 0=uniform..1=logarithmic; higher = sharper up close
            // Tune both live in the Lighting panel (Shadow Mapping > Cascades).
            moonlight->shadowConfig.shadowDistance = 200.0f;
            moonlight->shadowConfig.splitLambda    = 0.90f;
            moonlight->shadowConfig.casterExtrude  = 50.0f;
            // Acne-safe starting bias; normalBias is the primary acne lever. Tune live.
            moonlight->shadowConfig.bias = 0.0015f;
            moonlight->shadowConfig.normalBias = 0.03f;

            GetRenderer()->SetShadowConfig(moonlight->shadowConfig);

            // Moon — a bright emissive sphere placed opposite the moonlight
            // direction, giving the HDR/bloom pipeline an in-scene source to glow
            // against. Rendered through the Mesh pipeline's emissive branch
            // (customData.w >= 2.0 => unlit + HDR intensity); its color comes from
            // the bound white texture. Placed well beyond shadowDistance so it
            // never lands in a shadow cascade.
            {
                auto* moonVB = GetRenderer()->GetMoonSphereVertexBuffer();
                auto* moonIB = GetRenderer()->GetMoonSphereIndexBuffer();
                uint32_t moonIC = GetRenderer()->GetMoonSphereIndexCount();
                if (moonVB && moonIB && moonIC > 0)
                {
                    glm::vec3 moonDir = glm::normalize(moonlight->direction);
                    glm::vec3 moonPos = -moonDir * kMoonDistance; // source sits opposite its travel dir

                    auto moon = std::make_unique<MeshDrawable>(
                        moonVB, moonIB, moonIC, PipelineType::Mesh);
                    glm::mat4 moonTransform =
                        glm::translate(glm::mat4(1.0f), moonPos) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(kMoonRadius));
                    moon->SetTransform(moonTransform);
                    // rgb = HDR emissive color (white * intensity), w = 2.0 emissive flag.
                    // The Day/Night panel overrides rgb each frame (warm/dim by time of day).
                    moon->SetCustomData(glm::vec4(glm::vec3(kMoonIntensity), 2.0f));
                    if (auto* white = resources->GetTexture("default_white"))
                        moon->AddTexture(white);
                    SceneObject* moonObj = m_EditorScene->AddPrimitive("Moon", std::move(moon));
                    moonObj->pipeline = PipelineType::Mesh;
                    moonObj->primitiveTransform = moonTransform;
                    moonObj->primitiveKind = PrimitiveKind::MoonSphere;
                    moonObj->primitiveTexture = "default_white";
                    LOG_INFO("Added Moon to scene");
                }
            }

            if (m_EditorScene->GetObjectCount() > 0)
                m_EditorScene->Select(0);
        }

        void OnUpdate(float deltaTime) override
        {
            // Process a deferred scene load / new BEFORE the frame's draw list is built
            // in OnRender. These free the current scene's GPU buffers, and the previous
            // frame's draw list held raw pointers into them — so we wait for the GPU to
            // go idle first, then rebuild. Doing this mid-OnRender (from the menu) would
            // free buffers this frame's draw list still references.
            ProcessPendingSceneOps();

            // FPS tracking
            m_FrameTime += deltaTime;
            m_FrameCount++;
            if (m_FrameTime >= 1.0f)
            {
                m_FPS = m_FrameCount / m_FrameTime;
                m_FrameTime = 0.0f;
                m_FrameCount = 0;
                UpdateWindowTitle();
            }

            // Camera control (right-mouse-held)
            if (GetInput()->IsDown(InputCode::Mouse_Right))
            {
                m_CameraControlActive = true;
                glm::vec2 mouseDelta(GetInput()->GetMouseDeltaX(), GetInput()->GetMouseDeltaY());
                m_Camera->SetRotationInput(glm::vec2(mouseDelta.x, -mouseDelta.y));

                glm::vec3 moveInput(0.0f);
                if (GetInput()->IsDown(InputCode::Key_W)) moveInput.z += 1.0f;
                if (GetInput()->IsDown(InputCode::Key_S)) moveInput.z -= 1.0f;
                if (GetInput()->IsDown(InputCode::Key_D)) moveInput.x += 1.0f;
                if (GetInput()->IsDown(InputCode::Key_A)) moveInput.x -= 1.0f;
                if (GetInput()->IsDown(InputCode::Key_E) ||
                    GetInput()->IsDown(InputCode::Key_Space)) moveInput.y += 1.0f;
                if (GetInput()->IsDown(InputCode::Key_Q) ||
                    GetInput()->IsDown(InputCode::Key_Control)) moveInput.y -= 1.0f;

                m_Camera->SetMovementInput(moveInput);
                m_Camera->isSprinting = GetInput()->IsDown(InputCode::Key_Shift);
            }
            else
            {
                if (m_CameraControlActive)
                {
                    m_Camera->SetRotationInput(glm::vec2(0.0f));
                    m_Camera->SetMovementInput(glm::vec3(0.0f));
                    m_CameraControlActive = false;
                }
            }

            m_Camera->Update(deltaTime);

            // Day/night cycle: drives lights[0] direction/color/intensity + ambient
            // from the time-of-day slider, and positions/tints the moon disc. Runs
            // before BuildLightingData below so its changes take effect this frame.
            if (m_EditorScene) m_DayNight.Apply(*m_EditorScene, deltaTime);

            // Push camera/lighting to renderer
            GetRenderer()->SetViewMatrix(m_Camera->GetViewMatrix());
            GetRenderer()->SetProjectionMatrix(m_Camera->GetProjectionMatrix());
            GetRenderer()->SetCameraPosition(m_Camera->GetPosition());
            GetRenderer()->SetLightingData(m_EditorScene->BuildLightingData());

            // Sync shadow config in case editor changed it
            if (m_EditorScene->GetLightCount() > 0)
            {
                GetRenderer()->SetShadowConfig(m_EditorScene->GetLight(0)->shadowConfig);
            }

            // Update live shader test panel (owns rotation state)
            m_LiveShaderTest.Update(deltaTime);

            // Rotate the ToyCar using panel's rotation
            if (auto* toyCar = m_EditorScene->GetObject(0))
            {
                if (toyCar->model)
                {
                    toyCar->model->SetRotation(
                        glm::vec3(glm::radians(90.f), m_LiveShaderTest.GetRotation(), 0));
                }
            }

            if (m_EditorScene) m_EditorScene->Update(deltaTime);

            HandleEditorShortcuts();
        }

        void OnRender() override
        {
            DrawList drawList;
            glm::mat4 viewProj = m_Camera->GetProjectionMatrix() * m_Camera->GetViewMatrix();
            Frustum frustum = Frustum::ExtractFromMatrix(viewProj);

            if (m_EditorScene)
            {
                m_EditorScene->BuildDrawList(drawList, &frustum);
            }

            RenderEditorUI();

            m_TerrainPanel.SubmitTerrainDraw(drawList, m_Camera->GetPosition());
            m_GrassPanel.SubmitGrassDraw(drawList, frustum, m_TerrainPanel.GetTerrainSystem(), m_Camera->GetPosition());
            m_FireflyPanel.SubmitFireflyDraw(drawList);
            m_CloudPanel.SubmitCloudDraw(drawList);
            m_WaterPanel.SubmitWaterDraw(drawList);
            drawList.SortByPipeline();
            GetRenderer()->SubmitDrawList(drawList);
        }

    private:
        // Core editor state
        std::unique_ptr<Scene>  m_EditorScene;
        std::unique_ptr<Camera> m_Camera;
        bool m_CameraControlActive = false;

        // Project info
        std::string           m_CurrentProjectName = "Sandbox";
        std::filesystem::path m_CurrentProjectPath;

        // M1 project concept (see .claude/PACKAGING_ROADMAP.md). m_Project is the
        // active project; opening one loads its start scene. m_EngineRoot is where the
        // bundled "Sample" project + new projects are scaffolded (set in
        // SetupDefaultProject). Project switches are DEFERRED like scene loads because
        // they load a scene (freeing in-flight GPU buffers).
        Project                  m_Project;
        std::filesystem::path    m_EngineRoot;
        std::vector<std::string> m_RecentProjects;   // most-recent first, capped
        std::string              m_PendingProjectPath;
        bool                     m_PendingProjectOpen = false;

        static constexpr const char* kProjectFilter =
            "Nightbloom Project (*.nbproj)\0*.nbproj\0All Files (*.*)\0*.*\0";

        // Scene persistence (M0). m_ScenePath is the current scene file (empty =
        // untitled). Load/new are DEFERRED to the next OnUpdate (see
        // ProcessPendingSceneOps) so they never run while this frame's draw list
        // still references the scene's soon-to-be-freed GPU buffers.
        std::string m_ScenePath;
        std::string m_PendingLoadPath;
        bool        m_PendingSceneLoad = false;
        bool        m_PendingNewScene = false;

        // Win32 double-null-terminated filter for the scene file dialogs.
        static constexpr const char* kSceneFilter =
            "Nightbloom Scene (*.nbscene)\0*.nbscene\0All Files (*.*)\0*.*\0";

        // FPS tracking
        float m_FrameTime = 0.0f;
        int   m_FrameCount = 0;
        float m_FPS = 0.0f;

        // Moon — emissive sphere anchored opposite the moonlight (lights[0])
        // direction. Re-anchored every frame (see OnUpdate) so it tracks the
        // light direction live, which is also the hook a day/night cycle uses.
        static constexpr float kMoonDistance  = 350.0f;
        static constexpr float kMoonRadius    = 45.0f;
        static constexpr float kMoonIntensity = 5.0f;

        // ImGui demo/metrics (owned by EditorApp, not panels)
        bool m_ShowDemoWindow = false;
        bool m_ShowMetricsWindow = false;

        // Panels
        ConsolePanel            m_Console;
        AssetBrowserPanel       m_AssetBrowser;
        ViewportPanel           m_Viewport;
        DebugPanel              m_DebugPanel;
        SceneHierarchyPanel     m_SceneHierarchy;
        InspectorPanel          m_Inspector;
        LightingPanel           m_Lighting;
        DayNightPanel           m_DayNight;
        ProjectSettingsPanel    m_ProjectSettings;
        NoiseDebugPanel         m_NoiseDebug;
        ShaderCompilerPanel     m_ShaderCompiler;
        LiveShaderTestPanel     m_LiveShaderTest;
        TerrainPanel            m_TerrainPanel;
        GrassPanel              m_GrassPanel;
        FireflyPanel            m_FireflyPanel;
        CloudPanel              m_CloudPanel;
        WaterEditorPanel        m_WaterPanel;

        // -------------------------------------------------------------------------
        // RenderEditorUI � builds the EditorContext and calls into each panel
        // -------------------------------------------------------------------------
        void RenderEditorUI()
        {
            EditorContext ctx;
            ctx.scene = m_EditorScene.get();
            ctx.renderer = GetRenderer();
            ctx.projectName = &m_CurrentProjectName;
            ctx.projectPath = &m_CurrentProjectPath;

            DrawMainMenuBar();

#if USE_IMGUI_DOCKING
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);

            ImGuiWindowFlags windowFlags =
                ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

            ImGui::Begin("DockSpace", nullptr, windowFlags);
            ImGui::PopStyleVar(3);
            ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
            ImGui::End();
#endif

            // Draw panels
            if (m_SceneHierarchy.isOpen)  m_SceneHierarchy.Draw(ctx);
            if (m_Inspector.isOpen)       m_Inspector.Draw(ctx);
            if (m_Console.isOpen)         m_Console.Draw(ctx);
            if (m_AssetBrowser.isOpen)    m_AssetBrowser.Draw(ctx);
            if (m_ProjectSettings.isOpen) m_ProjectSettings.Draw(ctx);
            if (m_DebugPanel.isOpen)      m_DebugPanel.Draw(ctx);
            if (m_NoiseDebug.isOpen)      m_NoiseDebug.Draw(ctx);
            if (m_LiveShaderTest.isOpen)  m_LiveShaderTest.Draw(ctx);
            if (m_Lighting.isOpen)        m_Lighting.Draw(ctx);
            if (m_DayNight.isOpen)        m_DayNight.Draw(ctx);
            if (m_TerrainPanel.isOpen) m_TerrainPanel.Draw(ctx);
            if (m_GrassPanel.isOpen) m_GrassPanel.Draw(ctx, m_TerrainPanel.GetTerrainSystem());
            if (m_FireflyPanel.isOpen) m_FireflyPanel.Draw(ctx);
            if (m_CloudPanel.isOpen) m_CloudPanel.Draw(ctx);
            if (m_WaterPanel.isOpen) m_WaterPanel.Draw(ctx);

            m_Viewport.isPlayMode = m_Viewport.isPlayMode; // carried from menu bar
            m_Viewport.Draw(ctx);

            m_ShaderCompiler.Draw(ctx);

            if (m_ShowDemoWindow)    ImGui::ShowDemoWindow(&m_ShowDemoWindow);
            if (m_ShowMetricsWindow) ImGui::ShowMetricsWindow(&m_ShowMetricsWindow);
        }

        // -------------------------------------------------------------------------
        // Main menu bar � only place that controls panel visibility
        // -------------------------------------------------------------------------
        void DrawMainMenuBar()
        {
            if (!ImGui::BeginMainMenuBar()) return;

            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Project...")) NewProjectDialog();
                if (ImGui::MenuItem("Open Project...")) OpenProjectDialog();
                if (ImGui::MenuItem("Save Project")) SaveProject();
                if (ImGui::BeginMenu("Recent Projects", !m_RecentProjects.empty()))
                {
                    for (const std::string& recent : m_RecentProjects)
                    {
                        if (ImGui::MenuItem(recent.c_str()))
                        {
                            m_PendingProjectPath = recent;
                            m_PendingProjectOpen = true;
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("New Scene", "Ctrl+N")) RequestNewScene();
                if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) OpenSceneDialog();
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene();
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) SaveSceneAs();
                ImGui::Separator();
                if (ImGui::MenuItem("Project Settings...")) m_ProjectSettings.isOpen = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) Quit();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Play", "F5")) TogglePlayMode();
                if (ImGui::MenuItem("Stop", "F7", false, m_Viewport.isPlayMode)) StopPlayMode();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Tools"))
            {
                if (ImGui::MenuItem("Shader Editor", nullptr, &m_ShaderCompiler.isOpen)) {}
                if (ImGui::MenuItem("Asset Browser", nullptr, &m_AssetBrowser.isOpen)) {}
                ImGui::Separator();
                if (ImGui::MenuItem("Reload Shaders", "Ctrl+R"))
                    if (GetRenderer()) GetRenderer()->ReloadShaders();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Scene Hierarchy", nullptr, &m_SceneHierarchy.isOpen);
                ImGui::MenuItem("Inspector", nullptr, &m_Inspector.isOpen);
                ImGui::MenuItem("Console", nullptr, &m_Console.isOpen);
                ImGui::MenuItem("Lighting", nullptr, &m_Lighting.isOpen);
                ImGui::MenuItem("Day / Night", nullptr, &m_DayNight.isOpen);
                ImGui::MenuItem("Noise Debug", nullptr, &m_NoiseDebug.isOpen);
                ImGui::MenuItem("Terrain", nullptr, &m_TerrainPanel.isOpen);
                ImGui::MenuItem("Grass", nullptr, &m_GrassPanel.isOpen);
                ImGui::MenuItem("Fireflies", nullptr, &m_FireflyPanel.isOpen);
                ImGui::MenuItem("Clouds", nullptr, &m_CloudPanel.isOpen);
                ImGui::MenuItem("Water", nullptr, &m_WaterPanel.isOpen);
                ImGui::MenuItem("Live Shader Test", nullptr, &m_LiveShaderTest.isOpen);
                ImGui::Separator();
                ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow);
                ImGui::MenuItem("ImGui Metrics", nullptr, &m_ShowMetricsWindow);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Debug"))
            {
                ImGui::MenuItem("Debug Panel", nullptr, &m_DebugPanel.isOpen);
                ImGui::Separator();
                if (ImGui::MenuItem("Reload Shaders", "Ctrl+R"))
                    if (GetRenderer()) GetRenderer()->ReloadShaders();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("Documentation")) {}
                if (ImGui::MenuItem("About")) { LOG_INFO("Nightbloom Editor v0.1.0"); }
                ImGui::EndMenu();
            }

            // Right side � play controls
            float menuWidth = ImGui::GetWindowWidth();
            if (menuWidth > 400)
            {
                ImGui::SameLine(menuWidth - 350);
                ImGui::Text("Project: %s", m_CurrentProjectName.c_str());
                ImGui::SameLine(menuWidth - 200);

                if (!m_Viewport.isPlayMode)
                {
                    if (ImGui::Button("Play")) TogglePlayMode();
                }
                else
                {
                    if (ImGui::Button("Stop")) StopPlayMode();
                }
            }

            ImGui::EndMainMenuBar();
        }

        // -------------------------------------------------------------------------
        // Keyboard shortcuts
        // -------------------------------------------------------------------------
        void HandleEditorShortcuts()
        {
            if (!GetInput()) return;
            bool ctrl = GetInput()->IsDown(InputCode::Key_Control);

            if (ctrl && GetInput()->IsPressed(InputCode::Key_R))
                if (GetRenderer()) GetRenderer()->ReloadShaders();

            if (ctrl && GetInput()->IsPressed(InputCode::Key_S))
            {
                if (GetInput()->IsDown(InputCode::Key_Shift)) SaveSceneAs();
                else SaveScene();
            }

            if (GetInput()->IsPressed(InputCode::Key_P))
                if (GetRenderer()) GetRenderer()->TogglePipeline();

            if (GetInput()->IsPressed(InputCode::Key_F5)) TogglePlayMode();
            if (GetInput()->IsPressed(InputCode::Key_F7)) StopPlayMode();

            if (GetInput()->IsPressed(InputCode::Key_Escape))
            {
                LOG_INFO("Escape pressed - exiting editor");
                Quit();
            }
        }

        // -------------------------------------------------------------------------
        // Play mode
        // -------------------------------------------------------------------------
        void TogglePlayMode()
        {
            m_Viewport.isPlayMode = !m_Viewport.isPlayMode;
            LOG_INFO(m_Viewport.isPlayMode ? "Entering play mode" : "Exiting play mode");
        }

        void StopPlayMode()
        {
            if (m_Viewport.isPlayMode)
            {
                m_Viewport.isPlayMode = false;
                LOG_INFO("Stopping play mode");
            }
        }

        // -------------------------------------------------------------------------
        // Project setup
        // -------------------------------------------------------------------------
        void SetupDefaultProject()
        {
            std::filesystem::path currentPath = std::filesystem::current_path();
            std::filesystem::path exePath = std::filesystem::canonical(currentPath);

            LOG_INFO("Current working directory: {}", currentPath.string());

            std::filesystem::path engineRoot;
            std::filesystem::path searchPath = exePath;

            while (searchPath.has_parent_path())
            {
                if (std::filesystem::exists(searchPath / ".git") &&
                    std::filesystem::exists(searchPath / "Setup.bat"))
                {
                    engineRoot = searchPath;
                    LOG_INFO("Found engine root at: {}", engineRoot.string());
                    break;
                }
                searchPath = searchPath.parent_path();
            }

            m_EngineRoot = engineRoot;  // may be empty; BootProject falls back to cwd

            if (!engineRoot.empty())
            {
                m_CurrentProjectPath = engineRoot / "Sandbox";
                m_CurrentProjectName = "Sandbox";
            }
            else
            {
                LOG_WARN("Could not find engine root, using fallback path");
                m_CurrentProjectPath =
                    std::filesystem::path("D:/GitLibrary/Personal/NightBloom_Engine/Sandbox");
                m_CurrentProjectName = "Sandbox";
            }

            Editor::EditorFileUtils::ProjectContext ctx;
            ctx.root = m_CurrentProjectPath;
            ctx.config = "Debug";
#ifndef _DEBUG
            ctx.config = "Release";
#endif
            Editor::EditorFileUtils::SetProjectContext(ctx);

            LOG_INFO("Project: {} at {}", m_CurrentProjectName, m_CurrentProjectPath.string());
        }

        void UpdateWindowTitle()
        {
            std::string title = "Nightbloom Editor v0.1.0 | Project: " + m_CurrentProjectName;
            GetWindow()->SetTitle(title);
        }

        // -------------------------------------------------------------------------
        // M1 project management (see .claude/PACKAGING_ROADMAP.md)
        // -------------------------------------------------------------------------

        // Where bundled + newly-created projects live: <engineRoot>/Projects, or
        // ./Projects if the engine root wasn't found.
        std::filesystem::path GetProjectsRoot() const
        {
            std::filesystem::path base = m_EngineRoot.empty()
                ? std::filesystem::current_path() : m_EngineRoot;
            return base / "Projects";
        }

        // Boot the editor into a project: the last-opened one if it still exists,
        // otherwise the bundled Sample (seeded from BuildDefaultScene on first run).
        void BootProject()
        {
            LoadEditorSettings();

            for (const std::string& recent : m_RecentProjects)
            {
                if (!recent.empty() && std::filesystem::exists(recent))
                {
                    if (OpenProject(recent))
                        return;
                }
            }

            std::filesystem::path sample = EnsureSampleProject();
            if (!sample.empty() && OpenProject(sample.string()))
                return;

            // Ultimate fallback: an in-memory demo scene with no project on disk.
            LOG_WARN("BootProject: no project available; using in-memory default scene");
            BuildDefaultScene();
        }

        // Create the bundled Sample project on first run (folder + .nbproj + start
        // scene seeded from the demo scene). Returns the .nbproj path, or "" on error.
        std::filesystem::path EnsureSampleProject()
        {
            std::filesystem::path projectsRoot = GetProjectsRoot();
            std::filesystem::path sampleProj = projectsRoot / "Sample" / "Sample.nbproj";

            if (std::filesystem::exists(sampleProj))
                return sampleProj;

            Project p;
            if (!Project::Create(projectsRoot, "Sample", p))
                return {};

            // Seed the start scene from the demo content, then persist it.
            BuildDefaultScene();
            SceneCameraState cam;
            cam.position = m_Camera->GetPosition();
            cam.yaw = m_Camera->GetYaw();
            cam.pitch = m_Camera->GetPitch();
            cam.fov = m_Camera->GetFov();
            cam.nearPlane = m_Camera->GetNearPlane();
            SceneSerializer::Save(*m_EditorScene, cam, p.StartScenePath().string(),
                p.Settings().name, SerializeEditorState());

            LOG_INFO("Seeded Sample project at {}", p.Root().string());
            return p.File();
        }

        // Load a project and its start scene. Safe to call directly at startup (no
        // in-flight frame); at runtime, route through the pending-open path instead.
        bool OpenProject(const std::string& projectFile)
        {
            Project p;
            if (!Project::Load(projectFile, p))
                return false;

            m_Project = p;
            m_CurrentProjectName = p.Settings().name;
            m_CurrentProjectPath = p.Root();
            UpdateWindowTitle();

            // DoLoadScene waits for GPU idle and sets m_ScenePath on success.
            if (std::filesystem::exists(p.StartScenePath()))
                DoLoadScene(p.StartScenePath().string());
            else
                LOG_WARN("Project '{}' start scene missing: {}",
                    p.Settings().name, p.StartScenePath().string());

            AddRecentProject(projectFile);
            SaveEditorSettings();
            return true;
        }

        // File > New Project… — pick a location/name, scaffold it, seed an empty
        // start scene, then switch to it (deferred).
        void NewProjectDialog()
        {
            std::string path = Editor::EditorFileUtils::SaveFileDialog(kProjectFilter, "nbproj");
            if (path.empty()) return;

            std::filesystem::path pf(path);
            std::string name = pf.stem().string();
            std::filesystem::path parent = pf.parent_path();

            Project p;
            if (!Project::Create(parent, name, p))
            {
                LOG_ERROR("Failed to create project '{}'", name);
                return;
            }

            // Seed a minimal start scene (ambient + one directional light) with no GPU
            // interaction, so the new project isn't a black void when opened.
            Scene seed;
            seed.SetAmbient(glm::vec3(0.03f, 0.03f, 0.05f), 1.0f);
            Light* sun = seed.AddLight("Sun", LightType::Directional);
            sun->direction = glm::vec3(-0.3f, -0.8f, -0.5f);
            sun->color = glm::vec3(1.0f, 0.96f, 0.9f);
            sun->intensity = 1.0f;
            sun->shadowConfig.castsShadows = true;
            SceneCameraState cam;
            SceneSerializer::Save(seed, cam, p.StartScenePath().string(), name, "{}");

            m_PendingProjectPath = p.File().string();
            m_PendingProjectOpen = true;
        }

        void OpenProjectDialog()
        {
            std::string path = Editor::EditorFileUtils::OpenFileDialog(kProjectFilter);
            if (path.empty()) return;
            m_PendingProjectPath = path;
            m_PendingProjectOpen = true;
        }

        // Persist the project settings file and the current scene to its start scene.
        void SaveProject()
        {
            if (m_Project.IsValid())
                m_Project.Save();
            SaveScene();
        }

        void AddRecentProject(const std::string& projectFile)
        {
            std::filesystem::path abs = std::filesystem::absolute(projectFile);
            std::string key = abs.string();
            m_RecentProjects.erase(
                std::remove(m_RecentProjects.begin(), m_RecentProjects.end(), key),
                m_RecentProjects.end());
            m_RecentProjects.insert(m_RecentProjects.begin(), key);
            if (m_RecentProjects.size() > 8)
                m_RecentProjects.resize(8);
        }

        // -------------------------------------------------------------------------
        // Scene save / load / new (M0 — see .claude/PACKAGING_ROADMAP.md)
        // Save runs inline (read-only). Open/New are deferred to the next OnUpdate
        // because they free the current scene's GPU buffers (see ProcessPendingSceneOps).
        // -------------------------------------------------------------------------

        // Save to the current file, or prompt if this scene has never been saved.
        void SaveScene()
        {
            if (m_ScenePath.empty()) { SaveSceneAs(); return; }
            DoSaveScene(m_ScenePath);
        }

        void SaveSceneAs()
        {
            std::string path = Editor::EditorFileUtils::SaveFileDialog(kSceneFilter, "nbscene");
            if (path.empty()) return;  // cancelled
            m_ScenePath = path;
            DoSaveScene(path);
        }

        void OpenSceneDialog()
        {
            std::string path = Editor::EditorFileUtils::OpenFileDialog(kSceneFilter);
            if (path.empty()) return;  // cancelled
            m_PendingLoadPath = path;
            m_PendingSceneLoad = true;
        }

        void RequestNewScene() { m_PendingNewScene = true; }

        void DoSaveScene(const std::string& path)
        {
            if (!m_EditorScene || !m_Camera) return;

            SceneCameraState cam;
            cam.position = m_Camera->GetPosition();
            cam.yaw = m_Camera->GetYaw();
            cam.pitch = m_Camera->GetPitch();
            cam.fov = m_Camera->GetFov();
            cam.nearPlane = m_Camera->GetNearPlane();

            SceneSerializer::Save(*m_EditorScene, cam, path, m_CurrentProjectName,
                SerializeEditorState());
        }

        void ProcessPendingSceneOps()
        {
            // A project switch loads a scene (freeing GPU buffers), so it goes through
            // the same deferred path and takes precedence over a stale scene op.
            if (m_PendingProjectOpen)
            {
                m_PendingProjectOpen = false;
                m_PendingSceneLoad = false;
                m_PendingNewScene = false;
                OpenProject(m_PendingProjectPath);
                return;
            }
            if (m_PendingNewScene)
            {
                m_PendingNewScene = false;
                DoNewScene();
            }
            if (m_PendingSceneLoad)
            {
                m_PendingSceneLoad = false;
                DoLoadScene(m_PendingLoadPath);
            }
        }

        void DoLoadScene(const std::string& path)
        {
            if (!m_EditorScene || !m_Camera || !GetRenderer()) return;
            if (!std::filesystem::exists(path))
            {
                LOG_WARN("No scene file to load at '{}'", path);
                return;
            }

            // GPU must be idle before Load() frees the current scene's model buffers.
            GetRenderer()->WaitForIdle();

            SceneCameraState cam;
            cam.position = m_Camera->GetPosition();
            cam.yaw = m_Camera->GetYaw();
            cam.pitch = m_Camera->GetPitch();
            cam.fov = m_Camera->GetFov();
            cam.nearPlane = m_Camera->GetNearPlane();

            std::string editorState;
            if (SceneSerializer::Load(*m_EditorScene, cam, path, GetRenderer(), &editorState))
            {
                m_ScenePath = path;
                ApplyCameraState(cam);
                ApplyEditorState(editorState);

                // Re-sync shadow config from the (new) primary light, as OnStartup does.
                if (m_EditorScene->GetLightCount() > 0)
                    GetRenderer()->SetShadowConfig(m_EditorScene->GetLight(0)->shadowConfig);
            }
        }

        void DoNewScene()
        {
            if (!m_EditorScene || !m_Camera || !GetRenderer()) return;

            GetRenderer()->WaitForIdle();
            m_EditorScene->Clear();
            while (m_EditorScene->GetLightCount() > 0)
                m_EditorScene->RemoveLight(m_EditorScene->GetLightCount() - 1);
            m_EditorScene->SetAmbient(glm::vec3(0.03f, 0.03f, 0.05f), 1.0f);

            m_DayNight = DayNightPanel{};   // reset cycle to defaults (no GPU resources)
            m_ScenePath.clear();

            // Reset camera to the editor's default framing.
            m_Camera->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
            m_Camera->SetRotation(-135.0f, -20.0f);
            LOG_INFO("New (empty) scene");
        }

        void ApplyCameraState(const SceneCameraState& cam)
        {
            m_Camera->SetPosition(cam.position);
            m_Camera->SetRotation(cam.yaw, cam.pitch);
            uint32_t w = GetRenderer()->GetWidth();
            uint32_t h = GetRenderer()->GetHeight();
            float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : (1280.0f / 720.0f);
            m_Camera->SetPerspectiveInfiniteReverseZ(cam.fov, aspect, cam.nearPlane);
        }

        // --- DayNight panel state <-> the scene file's opaque "editor" blob ---
        std::string SerializeEditorState() const
        {
            auto v3 = [](const glm::vec3& v) { return nlohmann::json::array({ v.x, v.y, v.z }); };
            const DayNightPanel& d = m_DayNight;

            nlohmann::json dn;
            dn["enabled"] = d.enabled;
            dn["timeOfDay"] = d.timeOfDay;
            dn["autoAdvance"] = d.autoAdvance;
            dn["speedHours"] = d.speedHours;
            dn["arcTilt"] = d.arcTilt;
            dn["dayLightColor"] = v3(d.dayLightColor);
            dn["nightLightColor"] = v3(d.nightLightColor);
            dn["horizonColor"] = v3(d.horizonColor);
            dn["horizonStrength"] = d.horizonStrength;
            dn["dayLightIntensity"] = d.dayLightIntensity;
            dn["nightLightIntensity"] = d.nightLightIntensity;
            dn["dayAmbientColor"] = v3(d.dayAmbientColor);
            dn["nightAmbientColor"] = v3(d.nightAmbientColor);
            dn["dayAmbientIntensity"] = d.dayAmbientIntensity;
            dn["nightAmbientIntensity"] = d.nightAmbientIntensity;
            dn["dayDiscColor"] = v3(d.dayDiscColor);
            dn["nightDiscColor"] = v3(d.nightDiscColor);
            dn["dayDiscIntensity"] = d.dayDiscIntensity;
            dn["nightDiscIntensity"] = d.nightDiscIntensity;
            dn["discDistance"] = d.discDistance;
            dn["discRadius"] = d.discRadius;

            nlohmann::json root;
            root["dayNight"] = std::move(dn);
            return root.dump();
        }

        void ApplyEditorState(const std::string& stateJson)
        {
            if (stateJson.empty()) return;
            try
            {
                nlohmann::json root = nlohmann::json::parse(stateJson);
                if (!root.contains("dayNight")) return;
                const nlohmann::json& dn = root["dayNight"];

                auto rd3 = [](const nlohmann::json& a, glm::vec3 def) -> glm::vec3 {
                    if (a.is_array() && a.size() == 3)
                        return { a[0].get<float>(), a[1].get<float>(), a[2].get<float>() };
                    return def;
                };

                DayNightPanel& d = m_DayNight;
                d.enabled = dn.value("enabled", d.enabled);
                d.timeOfDay = dn.value("timeOfDay", d.timeOfDay);
                d.autoAdvance = dn.value("autoAdvance", d.autoAdvance);
                d.speedHours = dn.value("speedHours", d.speedHours);
                d.arcTilt = dn.value("arcTilt", d.arcTilt);
                d.dayLightColor = rd3(dn.value("dayLightColor", nlohmann::json::array()), d.dayLightColor);
                d.nightLightColor = rd3(dn.value("nightLightColor", nlohmann::json::array()), d.nightLightColor);
                d.horizonColor = rd3(dn.value("horizonColor", nlohmann::json::array()), d.horizonColor);
                d.horizonStrength = dn.value("horizonStrength", d.horizonStrength);
                d.dayLightIntensity = dn.value("dayLightIntensity", d.dayLightIntensity);
                d.nightLightIntensity = dn.value("nightLightIntensity", d.nightLightIntensity);
                d.dayAmbientColor = rd3(dn.value("dayAmbientColor", nlohmann::json::array()), d.dayAmbientColor);
                d.nightAmbientColor = rd3(dn.value("nightAmbientColor", nlohmann::json::array()), d.nightAmbientColor);
                d.dayAmbientIntensity = dn.value("dayAmbientIntensity", d.dayAmbientIntensity);
                d.nightAmbientIntensity = dn.value("nightAmbientIntensity", d.nightAmbientIntensity);
                d.dayDiscColor = rd3(dn.value("dayDiscColor", nlohmann::json::array()), d.dayDiscColor);
                d.nightDiscColor = rd3(dn.value("nightDiscColor", nlohmann::json::array()), d.nightDiscColor);
                d.dayDiscIntensity = dn.value("dayDiscIntensity", d.dayDiscIntensity);
                d.nightDiscIntensity = dn.value("nightDiscIntensity", d.nightDiscIntensity);
                d.discDistance = dn.value("discDistance", d.discDistance);
                d.discRadius = dn.value("discRadius", d.discRadius);
            }
            catch (const std::exception& e)
            {
                LOG_WARN("Failed to apply editor state from scene file: {}", e.what());
            }
        }

        // Global (project-independent) editor config: recent-projects list, so the
        // editor can reopen your last project on launch. Lives under %APPDATA% so it
        // survives clean rebuilds of the editor.
        std::filesystem::path GetEditorConfigPath() const
        {
            const char* appData = std::getenv("APPDATA");
            std::filesystem::path dir = appData
                ? std::filesystem::path(appData) / "Nightbloom"
                : std::filesystem::current_path();
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            return dir / "editor.json";
        }

        void LoadEditorSettings()
        {
            m_RecentProjects.clear();
            std::filesystem::path cfg = GetEditorConfigPath();
            if (!std::filesystem::exists(cfg)) return;

            std::ifstream file(cfg);
            if (!file.is_open()) return;
            try
            {
                nlohmann::json j;
                file >> j;
                if (j.contains("recentProjects"))
                    for (const auto& p : j["recentProjects"])
                        m_RecentProjects.push_back(p.get<std::string>());
            }
            catch (const std::exception& e)
            {
                LOG_WARN("Failed to parse editor config: {}", e.what());
            }
        }

        void SaveEditorSettings()
        {
            nlohmann::json j;
            j["recentProjects"] = m_RecentProjects;
            std::ofstream file(GetEditorConfigPath());
            if (file.is_open())
                file << j.dump(2);
        }
    };

} // namespace Nightbloom

// Entry point
Nightbloom::Application* Nightbloom::CreateApplication()
{
    return new Nightbloom::EditorApplication();
}