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
#include <memory>
#include <filesystem>

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

            // Scene
            m_EditorScene = std::make_unique<Scene>();
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

                auto cube2 = std::make_unique<MeshDrawable>(
                    vertexBuffer, indexBuffer, indexCount, PipelineType::Mesh);
                glm::mat4 cube2Transform =
                    glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.2f, -1.0f));
                cube2->SetTransform(cube2Transform);
                if (auto* white = resources->GetTexture("default_white"))
                    cube2->AddTexture(white);
                SceneObject* obj2 = m_EditorScene->AddPrimitive("TestCube2", std::move(cube2));
                obj2->textureIndex = 1;
                obj2->pipeline = PipelineType::Mesh;
                obj2->primitiveTransform = cube2Transform;
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
                    moon->SetCustomData(glm::vec4(0.0f, 0.0f, 0.0f, kMoonIntensity)); // w>=2 => emissive, blooms
                    if (auto* white = resources->GetTexture("default_white"))
                        moon->AddTexture(white);
                    SceneObject* moonObj = m_EditorScene->AddPrimitive("Moon", std::move(moon));
                    moonObj->pipeline = PipelineType::Mesh;
                    moonObj->primitiveTransform = moonTransform;
                    LOG_INFO("Added Moon to scene");
                }
            }

            if (m_EditorScene->GetObjectCount() > 0)
                m_EditorScene->Select(0);

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
        }

        void OnUpdate(float deltaTime) override
        {
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

            // Anchor the moon opposite the moonlight (lights[0]) direction so it
            // tracks the light live — drag the light's direction in the Lighting
            // panel (or animate it from a future day/night cycle) and the moon
            // follows. Looked up by name so it survives object add/remove.
            if (m_EditorScene->GetLightCount() > 0)
            {
                if (Light* moonlight = m_EditorScene->GetLight(0))
                {
                    for (auto& obj : m_EditorScene->GetObjects())
                    {
                        if (obj.name != "Moon" || !obj.meshDrawable) continue;
                        glm::vec3 dir = glm::normalize(moonlight->direction);
                        glm::mat4 t =
                            glm::translate(glm::mat4(1.0f), -dir * kMoonDistance) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(kMoonRadius));
                        obj.meshDrawable->SetTransform(t);
                        obj.primitiveTransform = t;
                        break;
                    }
                }
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
                if (ImGui::MenuItem("New Project...")) { LOG_INFO("New project - TODO"); }
                if (ImGui::MenuItem("Open Project...")) { LOG_INFO("Open project - TODO"); }
                if (ImGui::MenuItem("Save Project", "Ctrl+S")) { LOG_INFO("Save project - TODO"); }
                ImGui::Separator();
                if (ImGui::MenuItem("New Scene", "Ctrl+N")) { LOG_INFO("New scene - TODO"); }
                if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) { LOG_INFO("Open scene - TODO"); }
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) { LOG_INFO("Save scene - TODO"); }
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {}
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

        void LoadEditorSettings() { /* TODO */ }
        void SaveEditorSettings() { /* TODO */ }
    };

} // namespace Nightbloom

// Entry point
Nightbloom::Application* Nightbloom::CreateApplication()
{
    return new Nightbloom::EditorApplication();
}