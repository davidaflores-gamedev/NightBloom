//------------------------------------------------------------------------------
// EditorApp.cpp
//
// Nightbloom Editor Application - Fixed for ImGui compatibility
// Copyright (c) 2024. All rights reserved.
//------------------------------------------------------------------------------

#include "Engine/Core/Application.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include "Engine/Renderer/PipelineInterface.hpp"  // For pipeline management
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Tools/ShaderEditor/ShaderNodeEditor.hpp"
#include "EditorFileUtils.hpp"
#include <imgui.h>
#include <memory>
#include <filesystem>

#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/GLTFLoader.hpp"
#include "Engine/Renderer/Vulkan/VulkanShader.hpp"
#include "Engine/Renderer/Vulkan/VulkanPipeline.hpp"
#include "Engine/Renderer/Vulkan/VulkanPipelineAdapter.hpp"
#include "Engine/Renderer/Model.hpp"
#include "Engine/Renderer/Material.hpp"

#include "Engine/Renderer/Camera.hpp"

#include "Engine/Renderer/AssetManager.hpp"
//#include "Engine/Compute/Noisegenerator.hpp"

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

			// CRITICAL: Wait for GPU to finish before destroying model resources
			if (GetRenderer())
			{
				GetRenderer()->WaitForIdle();  // Make sure this method exists
			}

			// Clear the scene (this destroys all models/drawables)
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
				//GetWindow()->Maximize();
			}

			float aspect = 1280 / 720.0f;

			// In OnStartup()
			m_Camera = std::make_unique<Camera>();
			m_Camera->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
			m_Camera->SetRotation(-135.0f, -20.0f);  // Look toward origin
			m_Camera->SetPerspectiveInfiniteReverseZ(45.0f, aspect, 0.1f);
			GetRenderer()->SetCameraPosition(m_Camera->GetPosition());

			GetRenderer()->SetViewMatrix(m_Camera->GetViewMatrix());
			GetRenderer()->SetProjectionMatrix(m_Camera->GetProjectionMatrix());

			m_EditorScene = std::make_unique<Scene>();

			ResourceManager* resources = GetRenderer()->GetResourceManager();
			Texture* defaultTex = resources ? resources->GetTexture("default_white") : nullptr;

			// Load ToyCar model
			auto toyCar = std::make_unique<Model>("ToyCar");
			std::string modelPath = AssetManager::Get().GetModelPath("ToyCar/ToyCar.gltf");

			if (toyCar->LoadFromFile(modelPath,
				GetRenderer()->GetResourceManager(),
				GetRenderer()->GetDescriptorManager()))
			{
				toyCar->SetScale(0.01f);
				toyCar->SetPosition(glm::vec3(-3.0f, -2.0f, -3.0f));

				m_EditorScene->AddObject("ToyCar", std::move(toyCar), defaultTex);
				LOG_INFO("Added ToyCar to scene");
			}

			auto* vertexBuffer = GetRenderer()->GetTestVertexBuffer();  // Add getter
			auto* indexBuffer = GetRenderer()->GetTestIndexBuffer();    // Add getter
			uint32_t indexCount = GetRenderer()->GetTestIndexCount();   // Add getter

			//TestGLTFLoader();

			//LoadToyCar();

			if (vertexBuffer && indexBuffer && indexCount > 0)
			{
				// Cube 1 - UV Checker
				auto cube1 = std::make_unique<MeshDrawable>(
					vertexBuffer, indexBuffer, indexCount, PipelineType::Mesh
				);
				if (auto* checker = resources->GetTexture("uv_checker"))
				{
					cube1->AddTexture(checker);
				}
				SceneObject* obj1 = m_EditorScene->AddPrimitive("TestCube1", std::move(cube1));
				obj1->textureIndex = 0;  // UV Checker
				obj1->pipeline = PipelineType::Mesh;

				// Cube 2
				auto cube2 = std::make_unique<MeshDrawable>(
					vertexBuffer, indexBuffer, indexCount, PipelineType::Mesh
				);

				glm::mat4 cube2Transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.2f, -1.0f));
				cube2->SetTransform(cube2Transform);

				if (auto* white = resources->GetTexture("default_white"))
				{
					cube2->AddTexture(white);
				}
				SceneObject* obj2 = m_EditorScene->AddPrimitive("TestCube2", std::move(cube2));
				obj2->textureIndex = 1;  // White
				obj2->pipeline = PipelineType::Mesh;
				obj2->primitiveTransform = cube2Transform;

				LOG_INFO("Created 2 cubes sharing the same vertex/index buffers");
			}
			else
			{
				LOG_ERROR("Test geometry not available! vertexBuffer={}, indexBuffer={}, indexCount={}",
					(void*)vertexBuffer, (void*)indexBuffer, indexCount);
			}

			// === ADD: Ground plane ===
			auto* gpVB = GetRenderer()->GetGroundPlaneVertexBuffer();
			auto* gpIB = GetRenderer()->GetGroundPlaneIndexBuffer();
			uint32_t gpIC = GetRenderer()->GetGroundPlaneIndexCount();

			if (gpVB && gpIB && gpIC > 0)
			{
				auto groundPlane = std::make_unique<MeshDrawable>(
					gpVB, gpIB, gpIC, PipelineType::Mesh
				);

				// Position the plane below the car
				glm::mat4 groundTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.5f, 0.0f));
				groundPlane->SetTransform(groundTransform);

				// Use the white texture — the lighting will provide all the visual interest
				if (auto* white = resources->GetTexture("default_white"))
				{
					groundPlane->AddTexture(white);
				}

				SceneObject* groundObj = m_EditorScene->AddPrimitive("Ground", std::move(groundPlane));
				groundObj->textureIndex = 1;  // White
				groundObj->pipeline = PipelineType::Mesh;
				groundObj->primitiveTransform = groundTransform;

				LOG_INFO("Added ground plane to scene");
			}


			Light* moonlight = m_EditorScene->AddLight("Moonlight", LightType::Directional);
			moonlight->direction = glm::vec3(-0.3f, -0.8f, -0.5f);
			moonlight->color = glm::vec3(0.6f, 0.7f, 1.0f);
			moonlight->intensity = 0.8f;

			// Select first object by default
			if (m_EditorScene->GetObjectCount() > 0)
			{
				m_EditorScene->Select(0);
			}



			// Enable docking if available
			#if USE_IMGUI_DOCKING
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
			#endif

			m_ShaderNodeEditor = std::make_unique<ShaderNodeEditor>();

			GetWindow()->SetResizeCallback([this](uint32_t width, uint32_t height) {
				if (width > 0 && height > 0 && m_Camera)
				{
					float aspect = static_cast<float>(width) / static_cast<float>(height);
					m_Camera->SetPerspectiveInfiniteReverseZ(45.0f, aspect, 0.1f);
					LOG_INFO("Camera aspect ratio updated to {}", aspect);
				}
			});
		}

		void OnUpdate(float deltaTime) override
		{
			// Update editor systems
			m_FrameTime += deltaTime;
			m_FrameCount++;

			// Update title bar with stats every second
			if (m_FrameTime >= 1.0f)
			{
				float fps = m_FrameCount / m_FrameTime;
				m_FPS = fps;
				//std::string title = "Nightbloom Editor - FPS: " + std::to_string(static_cast<int>(fps));
				//GetWindow()->SetTitle(title);
				UpdateWindowTitle();
				m_FrameTime = 0.0f;
				m_FrameCount = 0;
			}

			// Camera controls (only when right mouse button held)
			if (GetInput()->IsDown(InputCode::Mouse_Right))
			{
				if (!m_CameraControlActive)
				{
					m_CameraControlActive = true;
					// Optionally hide/capture cursor here
				}

				// Rotation from mouse delta
				glm::vec2 mouseDelta = glm::vec2(GetInput()->GetMouseDeltaX(), GetInput()->GetMouseDeltaY());
				m_Camera->SetRotationInput(glm::vec2(mouseDelta.x, -mouseDelta.y));

				// Movement from WASD
				glm::vec3 moveInput(0.0f);
				if (GetInput()->IsDown(InputCode::Key_W)) moveInput.z += 1.0f;
				if (GetInput()->IsDown(InputCode::Key_S)) moveInput.z -= 1.0f;
				if (GetInput()->IsDown(InputCode::Key_D)) moveInput.x += 1.0f;
				if (GetInput()->IsDown(InputCode::Key_A)) moveInput.x -= 1.0f;
				if (GetInput()->IsDown(InputCode::Key_E) || GetInput()->IsDown(InputCode::Key_Space)) moveInput.y += 1.0f;
				if (GetInput()->IsDown(InputCode::Key_Q) || GetInput()->IsDown(InputCode::Key_Control)) moveInput.y -= 1.0f;

				m_Camera->SetMovementInput(moveInput);
				m_Camera->isSprinting = GetInput()->IsDown(InputCode::Key_Shift);
			}
			else
			{
				m_CameraControlActive = false;
			}

			// Update camera
			m_Camera->Update(deltaTime);

			SceneLightingData lightData = m_EditorScene->BuildLightingData();

			// Pass to renderer
			GetRenderer()->SetViewMatrix(m_Camera->GetViewMatrix());
			GetRenderer()->SetProjectionMatrix(m_Camera->GetProjectionMatrix());
			GetRenderer()->SetCameraPosition(m_Camera->GetPosition());
			GetRenderer()->SetLightingData(lightData);

			m_Rotation += deltaTime * m_RotationSpeed;

			if (auto* toyCar = m_EditorScene->GetObject(0))  // Assuming ToyCar is index 0
			{
				if (toyCar->model)
				{
					float baseRotationX = glm::radians(90.f);
					toyCar->model->SetRotation(glm::vec3(baseRotationX, m_Rotation, 0));
				}
			}

			// Update scene
			if (m_EditorScene)
			{
				m_EditorScene->Update(deltaTime);
			}

			// Handle editor shortcuts
			HandleEditorShortcuts();
		}

		void OnRender() override
		{
			DrawList drawList;

			// Add scene objects
			if (m_EditorScene)
			{
				m_EditorScene->BuildDrawList(drawList);
			}

			// Sort by pipeline for efficiency
			drawList.SortByPipeline();

			// Submit to renderer
			GetRenderer()->SubmitDrawList(drawList);

			// Render editor UI
			RenderEditorUI();
		}

	private:
		// For live shader testing
		float m_Rotation = 0.0f;
		float m_RotationSpeed = 1.0f;  // Speed of rotation for live shader test

		std::unique_ptr<Scene> m_EditorScene;

		std::unique_ptr<Camera> m_Camera;
		bool m_CameraControlActive = false;  // Only move camera when right-click held

		// Project state
		std::string m_CurrentProjectName = "Sandbox";
		std::filesystem::path m_CurrentProjectPath;

		// Editor state
		bool m_ShowDemoWindow = false;
		bool m_ShowMetricsWindow = false;
		bool m_ShowShaderNodeEditor = false;
		bool m_ShowAssetBrowser = false;
		bool m_ShowSceneHierarchy = true;
		bool m_ShowInspector = true;
		bool m_ShowConsole = true;
		bool m_ShowProjectSettings = false;
		bool m_ShowLiveShaderTest = true;
		bool m_ShowLightingPanel = true;

		bool m_ShowDebugPanel = false;
		bool m_ComputeTestRan = false;

		// Tools
		std::unique_ptr<ShaderNodeEditor> m_ShaderNodeEditor;

		//bool m_ShowNoiseGenerator = false;

		std::vector<VulkanTexture*> m_GeneratedNoiseTextures;

		PipelineType m_CurrentTestPipeline = PipelineType::Mesh;

		// Stats
		float m_FrameTime = 0.0f;
		int m_FrameCount = 0;
		float m_FPS = 0.0f;

		// Viewport state
		bool m_IsPlayMode = false;

		// Shader compilation state
		bool m_CompileSuccess = false;
		bool m_CompileError = false;
		std::string m_LastError;

		void SetupDefaultProject()
		{
			// First, let's understand exactly where we are
			std::filesystem::path currentPath = std::filesystem::current_path();
			std::filesystem::path exePath = std::filesystem::canonical(currentPath);

			LOG_INFO("=== PATH DEBUGGING ===");
			LOG_INFO("Current working directory: {}", currentPath.string());
			LOG_INFO("Canonical path: {}", exePath.string());

			// Let's trace up the path and see what exists at each level
			std::filesystem::path tracePath = exePath;
			int level = 0;
			while (tracePath.has_parent_path() && level < 10) {
				LOG_INFO("Level {}: {}", level, tracePath.string());

				// Check what exists at this level
				if (std::filesystem::exists(tracePath / "Sandbox")) {
					LOG_INFO("  -> Found Sandbox here!");
				}
				if (std::filesystem::exists(tracePath / "Editor")) {
					LOG_INFO("  -> Found Editor here!");
				}
				if (std::filesystem::exists(tracePath / "NightBloom")) {
					LOG_INFO("  -> Found NightBloom here!");
				}

				tracePath = tracePath.parent_path();
				level++;
			}
			LOG_INFO("=== END PATH DEBUGGING ===");

			// Now find the NightBloom_Engine root by looking for key markers
			std::filesystem::path engineRoot;
			std::filesystem::path searchPath = exePath;

			// Keep going up until we find a directory that contains BOTH Editor and Sandbox folders
			while (searchPath.has_parent_path()) {
				if (std::filesystem::exists(searchPath / ".git") &&
					std::filesystem::exists(searchPath / "Setup.bat")) {
					engineRoot = searchPath;
					LOG_INFO("Found NightBloom_Engine root at: {}", engineRoot.string());
					break;
				}
				searchPath = searchPath.parent_path();
			}

			if (!engineRoot.empty()) {
				m_CurrentProjectPath = engineRoot / "Sandbox";
				m_CurrentProjectName = "Sandbox";
				LOG_INFO("Using Sandbox at: {}", m_CurrentProjectPath.string());
			}
			else {
				LOG_WARN("Could not find engine root, using fallback");
				m_CurrentProjectPath = std::filesystem::path("D:/GitLibrary/Personal/NightBloom_Engine/Sandbox");
				m_CurrentProjectName = "Sandbox";
			}

			// Set up the project context for EditorFileUtils
			Editor::EditorFileUtils::ProjectContext ctx;
			ctx.root = m_CurrentProjectPath;

#ifdef _DEBUG
			ctx.config = "Debug";
#else
			ctx.config = "Release";
#endif

			Editor::EditorFileUtils::SetProjectContext(ctx);

			LOG_INFO("Current project: {} at {}", m_CurrentProjectName, m_CurrentProjectPath.string());
		}

		void UpdateWindowTitle()
		{
			std::string title = "Nightbloom Editor v0.1.0";
			title += " | Project: " + m_CurrentProjectName;

			//if (m_FPS > 0)
			//{
			//	title += " | FPS: " + std::to_string(static_cast<int>(m_FPS));
			//}

			GetWindow()->SetTitle(title);
		}

		void TestGLTFLoader()
		{
			LOG_INFO("=== Testing GLTF Loader ===");

			GLTFLoader loader;

			// Try to load a test model
			// You'll need to put a .gltf or .glb file in your Models folder
			std::string modelPath = AssetManager::Get().GetModelPath("ToyCar/ToyCar.gltf");

			auto modelData = loader.Load(modelPath);

			if (!modelData)
			{
				LOG_ERROR("GLTF Test FAILED: {}", loader.GetLastError());
				return;
			}

			// Log what we loaded
			LOG_INFO("GLTF Test SUCCESS!");
			LOG_INFO("  Model: {}", modelData->name);
			LOG_INFO("  Meshes: {}", modelData->meshes.size());
			LOG_INFO("  Materials: {}", modelData->materials.size());
			LOG_INFO("  Total Vertices: {}", modelData->totalVertices);
			LOG_INFO("  Total Indices: {}", modelData->totalIndices);

			// Log each mesh
			for (size_t i = 0; i < modelData->meshes.size(); ++i)
			{
				const auto& mesh = modelData->meshes[i];
				LOG_INFO("  Mesh[{}] '{}': {} verts, {} indices, bounds: ({:.2f},{:.2f},{:.2f}) to ({:.2f},{:.2f},{:.2f})",
					i, mesh.name,
					mesh.vertices.size(), mesh.indices.size(),
					mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMin.z,
					mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMax.z);

				// Log first few vertices to verify data looks correct
				if (!mesh.vertices.empty())
				{
					const auto& v = mesh.vertices[0];
					LOG_INFO("    First vertex: pos=({:.3f},{:.3f},{:.3f}), normal=({:.3f},{:.3f},{:.3f}), uv=({:.3f},{:.3f})",
						v.position.x, v.position.y, v.position.z,
						v.normal.x, v.normal.y, v.normal.z,
						v.texCoord.x, v.texCoord.y);
				}
			}

			// Log materials
			for (size_t i = 0; i < modelData->materials.size(); ++i)
			{
				const auto& mat = modelData->materials[i];
				LOG_INFO("  Material[{}] '{}': color=({:.2f},{:.2f},{:.2f},{:.2f}), metallic={:.2f}, roughness={:.2f}",
					i, mat.name,
					mat.baseColorFactor.r, mat.baseColorFactor.g, mat.baseColorFactor.b, mat.baseColorFactor.a,
					mat.metallicFactor, mat.roughnessFactor);

				if (!mat.baseColorTexturePath.empty())
					LOG_INFO("    Albedo texture: {}", mat.baseColorTexturePath);
			}

			LOG_INFO("=== GLTF Loader Test Complete ===");
		}

		void RenderEditorUI()
		{
			// Main menu bar
			if (ImGui::BeginMainMenuBar())
			{
				if (ImGui::BeginMenu("File"))
				{
					if (ImGui::MenuItem("New Project...")) { NewProject(); }
					if (ImGui::MenuItem("Open Project...")) { OpenProject(); }
					if (ImGui::MenuItem("Save Project", "Ctrl+S")) { SaveProject(); }
					ImGui::Separator();
					if (ImGui::MenuItem("New Scene", "Ctrl+N")) { NewScene(); }
					if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) { OpenScene(); }
					if (ImGui::MenuItem("Save Scene", "Ctrl+S")) { SaveScene(); }
					if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) { SaveSceneAs(); }
					ImGui::Separator();
					if (ImGui::MenuItem("Project Settings...")) { m_ShowProjectSettings = true; }
					ImGui::Separator();
					if (ImGui::MenuItem("Exit", "Alt+F4")) { Quit(); }
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Edit"))
				{
					if (ImGui::MenuItem("Play", "F5")) { TogglePlayMode(); }
					if (ImGui::MenuItem("Stop", "F7", false, m_IsPlayMode)) { StopPlayMode(); }
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Tools"))
				{
					if (ImGui::MenuItem("Shader Editor", nullptr, &m_ShowShaderNodeEditor)) {}
					if (ImGui::MenuItem("Asset Browser", nullptr, &m_ShowAssetBrowser)) {}
					ImGui::Separator();
					ImGui::Separator();
					if (ImGui::MenuItem("Compile All Shaders")) { CompileAllShaders(); }
					if (ImGui::MenuItem("Reload Shaders", "Ctrl+R"))
					{
						if (GetRenderer()) GetRenderer()->ReloadShaders();
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("View"))
				{
					if (ImGui::MenuItem("Scene Hierarchy", nullptr, &m_ShowSceneHierarchy)) {}
					if (ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector)) {}
					if (ImGui::MenuItem("Console", nullptr, &m_ShowConsole)) {}
					if (ImGui::MenuItem("Lighting", nullptr, &m_ShowLightingPanel)) {}
					ImGui::Separator();
					if (ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow)) {}
					if (ImGui::MenuItem("ImGui Metrics", nullptr, &m_ShowMetricsWindow)) {}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Debug"))
				{
					if (ImGui::MenuItem("Debug Panel", nullptr, &m_ShowDebugPanel)) {}
					ImGui::Separator();
					if (ImGui::MenuItem("Run Compute Test"))
					{
						if (GetRenderer())
						{
							GetRenderer()->PrintComputeTestResults();
							m_ComputeTestRan = true;
						}
					}
					if (ImGui::MenuItem("Reload Shaders", "Ctrl+R"))
					{
						if (GetRenderer()) GetRenderer()->ReloadShaders();
					}
					ImGui::EndMenu();
				}


				if (ImGui::BeginMenu("Help"))
				{
					if (ImGui::MenuItem("Documentation")) { OpenDocumentation(); }
					if (ImGui::MenuItem("About")) { ShowAboutDialog(); }
					ImGui::EndMenu();
				}

				// Right side of menu bar - play controls
				ImGui::Separator();
				float menuWidth = ImGui::GetWindowWidth();
				if (menuWidth > 400) {
					ImGui::SameLine(menuWidth - 350);
					ImGui::Text("Project: %s", m_CurrentProjectName.c_str());

					ImGui::SameLine(menuWidth - 200);

					if (!m_IsPlayMode)
					{
						if (ImGui::Button("Play")) { TogglePlayMode(); }
					}
					else
					{
						if (ImGui::Button("Stop")) { StopPlayMode(); }
					}
				}

				ImGui::EndMainMenuBar();
			}

#if USE_IMGUI_DOCKING
			// Dockspace - only if docking is available
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);
			ImGui::SetNextWindowViewport(viewport->ID);

			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar |
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

			// Editor panels (these work with or without docking)
			if (m_ShowSceneHierarchy) RenderSceneHierarchy();
			if (m_ShowInspector) RenderInspector();
			if (m_ShowConsole) RenderConsole();
			if (m_ShowAssetBrowser) RenderAssetBrowser();
			if (m_ShowProjectSettings) RenderProjectSettings();
			if (m_ShowDebugPanel) RenderDebugPanel();

			if (m_ShowShaderNodeEditor && m_ShaderNodeEditor)
			{
				m_ShaderNodeEditor->Draw("Shader Node Editor", &m_ShowShaderNodeEditor);

				RenderShaderCompiler();
			}



			if (m_ShowLiveShaderTest) RenderLiveShaderTest();

			if (m_ShowLightingPanel)
			{
				ImGui::Begin("Lighting", &m_ShowLightingPanel);
				RenderLightingPanel();
				ImGui::End();
			}

			// Always render viewport
			RenderSceneViewport();

			// Demo windows
			if (m_ShowDemoWindow) ImGui::ShowDemoWindow(&m_ShowDemoWindow);
			if (m_ShowMetricsWindow) ImGui::ShowMetricsWindow(&m_ShowMetricsWindow);
		}

		void RenderDebugPanel()
		{
			ImGui::Begin("Debug Panel", &m_ShowDebugPanel);

			ImGui::Text("Compute Pipeline Testing");
			ImGui::Separator();

			if (ImGui::Button("Run Compute Test", ImVec2(200, 30)))
			{
				if (GetRenderer())
				{
					GetRenderer()->PrintComputeTestResults();
					m_ComputeTestRan = true;
				}
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Runs a simple compute shader that multiplies 64 floats by 2.\nCheck the console/log for results.");
			}

			ImGui::Spacing();
			ImGui::Text("The compute test:");
			ImGui::BulletText("Input:  1, 2, 3, ... 64");
			ImGui::BulletText("Output: 2, 4, 6, ... 128");

			if (m_ComputeTestRan)
			{
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Test executed - check console for results");
			}

			ImGui::Separator();
			ImGui::Text("Shader Tools");

			if (ImGui::Button("Reload All Shaders", ImVec2(200, 25)))
			{
				if (GetRenderer())
				{
					GetRenderer()->ReloadShaders();
				}
			}

			if (ImGui::Button("Toggle Pipeline (P)", ImVec2(200, 25)))
			{
				if (GetRenderer())
				{
					GetRenderer()->TogglePipeline();
				}
			}

			ImGui::Separator();
			ImGui::Text("Memory & Performance");

			// You could add more debug info here later
			ImGui::Text("Press F3 to toggle performance overlay");

			ImGui::End();
		}

		void RenderShaderCompiler()
		{
			ImGui::Begin("Shader Compiler");

			ImGui::Text("Node Graph Shader Compiler");
			ImGui::Text("Target Project: %s", m_CurrentProjectName.c_str());
			ImGui::Separator();

			if (ImGui::Button("Compile Shader", ImVec2(150, 30)))
			{
				if (m_ShaderNodeEditor && m_ShaderNodeEditor->CompileShaders())
				{
					std::string vertShader = m_ShaderNodeEditor->GetVertexShader();
					std::string fragShader = m_ShaderNodeEditor->GetFragmentShader();

					// Save and compile shaders
					bool vertSuccess = Editor::EditorFileUtils::SaveShaderFile("NodeGenerated.vert", vertShader);
					bool fragSuccess = Editor::EditorFileUtils::SaveShaderFile("NodeGenerated.frag", fragShader);

					if (vertSuccess && fragSuccess)
					{
						LOG_INFO("Shaders compiled and deployed to project successfully!");
						m_CompileSuccess = true;
						m_CompileError = false;
						m_LastError.clear();

						// Create or reload the NodeGenerated pipeline
						PipelineConfig config;
						config.vertexShaderPath = "NodeGenerated.vert";
						config.fragmentShaderPath = "NodeGenerated.frag";
						config.useVertexInput = true;  // or false depending on your needs

						// Use generic enums
						config.topology = Nightbloom::PrimitiveTopology::TriangleList;
						config.polygonMode = Nightbloom::PolygonMode::Fill;
						config.cullMode = Nightbloom::CullMode::Back;
						config.frontFace = Nightbloom::FrontFace::CounterClockwise;
						config.depthTestEnable = true;
						config.depthWriteEnable = true;
						config.depthCompareOp = Nightbloom::CompareOp::GreaterOrEqual;
						config.blendEnable = false;
						config.useUniformBuffer = true;  // ADD THIS
						config.useTextures =true;
						config.pushConstantSize = sizeof(PushConstantData);
						config.pushConstantStages = Nightbloom::ShaderStage::VertexFragment;

						if (GetRenderer()->GetPipelineManager()->CreatePipeline(
							Nightbloom::PipelineType::NodeGenerated,config))
						{
							m_CurrentTestPipeline = PipelineType::NodeGenerated;

							// Update test cube
							if (m_EditorScene)
							{
								SceneObject* selected = m_EditorScene->GetSelected();
								if (selected && selected->meshDrawable)
								{
									// Recreate the drawable with the new pipeline
									ApplyPipelineToSelectedPrimitive(PipelineType::NodeGenerated);
									LOG_INFO("Applied NodeGenerated shader to selected primitive");
								}
							}
						}
					}
					else
					{
						LOG_ERROR("Failed to compile/deploy shaders");
						m_CompileSuccess = false;
						m_CompileError = true;
						m_LastError = "Failed to compile or deploy shaders to project";
					}
				}
				else
				{
					LOG_ERROR("Shader generation failed!");
					m_CompileSuccess = false;
					m_CompileError = true;
					if (m_ShaderNodeEditor)
					{
						m_LastError = m_ShaderNodeEditor->GetLastError();
					}
				}
			}

			ImGui::SameLine();

			if (ImGui::Button("Apply to Scene", ImVec2(150, 30)))
			{
				if (GetRenderer())
				{
					GetRenderer()->ReloadShaders();
					LOG_INFO("Reloaded shaders in renderer");
				}
			}

			// Show compile status
			if (m_CompileSuccess)
			{
				ImGui::TextColored(ImVec4(0, 1, 0, 1), "Compilation successful!");
				ImGui::Text("Shaders deployed to: %s/Shaders/", m_CurrentProjectName.c_str());
			}
			else if (m_CompileError)
			{
				ImGui::TextColored(ImVec4(1, 0, 0, 1), "Compilation failed!");
				if (!m_LastError.empty())
				{
					ImGui::TextWrapped("Error: %s", m_LastError.c_str());
				}
			}

			ImGui::Separator();
			ImGui::Text("Instructions:");
			ImGui::BulletText("Right-click canvas to add nodes");
			ImGui::BulletText("Drag from output to input pins");
			ImGui::BulletText("Middle mouse to pan");
			ImGui::BulletText("Select node + Delete to remove");
			ImGui::BulletText("Double-click node for properties");

			ImGui::End();
		}

		// ADD THIS NEW METHOD for live shader testing
		void RenderLiveShaderTest()
		{
			ImGui::Begin("Live Shader Test", &m_ShowLiveShaderTest);

			// Check if we have a selected primitive
			SceneObject* selected = m_EditorScene ? m_EditorScene->GetSelected() : nullptr;
			bool hasPrimitive = selected && selected->meshDrawable;

			if (!hasPrimitive)
			{
				ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
					"Select a primitive in the hierarchy to test shaders");
				ImGui::Text("(Models use their own materials)");
				ImGui::End();
				return;
			}

			ImGui::Text("Testing on: %s", selected->name.c_str());
			ImGui::Separator();

			// Pipeline selector
			const char* pipelineNames[] = {
				"Triangle", "Mesh", "Transparent", "Shadow", "Skybox",
				"Volumetric", "PostProcess", "Compute", "NodeGenerated"
			};

			int currentPipeline = (int)(selected->pipeline);
			if (ImGui::Combo("Pipeline", &currentPipeline, pipelineNames,
				static_cast<int>(PipelineType::Count)))
			{
				PipelineType newPipeline = static_cast<PipelineType>(currentPipeline);

				// Check if pipeline exists
				auto* pipelineManager = GetRenderer()->GetPipelineManager();
				if (pipelineManager->GetPipeline(newPipeline) == nullptr)
				{
					LOG_WARN("Pipeline {} does not exist!", pipelineNames[currentPipeline]);
				}
				else
				{
					selected->pipeline = newPipeline;
					m_CurrentTestPipeline = newPipeline;
					ApplyPipelineToSelectedPrimitive(newPipeline);
					LOG_INFO("Switched to {} pipeline", pipelineNames[currentPipeline]);
				}
			}

			ImGui::Separator();

			const char* textureNames[] = { "UV Checker", "White", "Black", "Normal" };
			const char* textureLookup[] = { "uv_checker", "default_white", "default_black", "default_normal" };

			if (ImGui::Combo("Texture", &selected->textureIndex, textureNames, 4))
			{
				ResourceManager* resources = GetRenderer()->GetResourceManager();
				if (resources && selected->meshDrawable)
				{
					selected->meshDrawable->ClearTextures();

					VulkanTexture* texture = resources->GetTexture(textureLookup[selected->textureIndex]);
					if (texture)
					{
						selected->meshDrawable->AddTexture(texture);
						LOG_INFO("Switched to {} texture", textureNames[selected->textureIndex]);
					}
				}
			}

			ImGui::Separator();

			// Rotation speed control (affects all objects via m_Rotation)
			ImGui::SliderFloat("Rotation Speed", &m_RotationSpeed, 0.0f, 5.0f);

			if (ImGui::Button("Reset Rotation"))
			{
				m_Rotation = 0.0f;
				m_RotationSpeed = 1.0f;
			}

			ImGui::End();
		}

		void RenderProjectSettings()
		{
			ImGui::Begin("Project Settings", &m_ShowProjectSettings);

			ImGui::Text("Current Project: %s", m_CurrentProjectName.c_str());
			ImGui::Text("Project Path: %s", m_CurrentProjectPath.string().c_str());

			ImGui::Separator();

			// Build configuration
			static int buildConfig = 0;
			const char* configs[] = { "Debug", "Release", "RelWithDebInfo" };
			if (ImGui::Combo("Build Configuration", &buildConfig, configs, 3))
			{
				Editor::EditorFileUtils::ProjectContext ctx = Editor::EditorFileUtils::GetProjectContext();
				ctx.config = configs[buildConfig];
				Editor::EditorFileUtils::SetProjectContext(ctx);
				LOG_INFO("Changed build configuration to: {}", configs[buildConfig]);
			}

			ImGui::Separator();

			// Shader settings
			ImGui::Text("Shader Settings");
			ImGui::Text("Source: %s", Editor::EditorFileUtils::GetEditorShadersSourceDirectory().c_str());
			ImGui::Text("Compiled: %s", Editor::EditorFileUtils::GetEditorShadersCompiledDirectory().c_str());
			ImGui::Text("Deploy to: %s", Editor::EditorFileUtils::GetCurrentProjectShadersDirectory().c_str());

			ImGui::End();
		}

		void RenderSceneHierarchy()
		{
			ImGui::Begin("Scene Hierarchy", &m_ShowSceneHierarchy);

			if (!m_EditorScene)
			{
				ImGui::Text("No scene loaded");
				ImGui::End();
				return;
			}

			// Scene root
			ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
			if (ImGui::TreeNodeEx("Scene", rootFlags))
			{
				auto& objects = m_EditorScene->GetObjects();
				int selectedIndex = m_EditorScene->GetSelectedIndex();

				for (size_t i = 0; i < objects.size(); ++i)
				{
					auto& obj = objects[i];

					ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

					// Highlight selected
					if (static_cast<int>(i) == selectedIndex)
					{
						nodeFlags |= ImGuiTreeNodeFlags_Selected;
					}

					// Dim if invisible
					if (!obj.visible)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
					}

					// Icon based on type
					const char* icon = obj.model ? "[M]" : "[P]";  // Model or Primitive

					ImGui::TreeNodeEx((void*)(intptr_t)i, nodeFlags, "%s %s", icon, obj.name.c_str());

					// Selection
					if (ImGui::IsItemClicked())
					{
						m_EditorScene->Select(static_cast<int>(i));
					}

					// Context menu
					if (ImGui::BeginPopupContextItem())
					{
						if (ImGui::MenuItem("Toggle Visibility"))
						{
							obj.visible = !obj.visible;
						}
						if (ImGui::MenuItem("Rename..."))
						{
							// TODO: Open rename dialog
						}
						ImGui::Separator();
						if (ImGui::MenuItem("Delete"))
						{
							m_EditorScene->RemoveObject(i);
							ImGui::EndPopup();

							if (!obj.visible)
							{
								ImGui::PopStyleColor();
							}
							break;  // Iterator invalidated
						}
						ImGui::EndPopup();
					}

					if (!obj.visible)
					{
						ImGui::PopStyleColor();
					}
				}

				ImGui::Separator();
				if (ImGui::TreeNodeEx("Lights", ImGuiTreeNodeFlags_DefaultOpen))
				{
					auto& lights = m_EditorScene->GetLights();
					int selectedLightIndex = m_EditorScene->GetSelectedLightIndex();

					for (size_t i = 0; i < lights.size(); ++i)
					{
						auto& light = lights[i];

						ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

						if (static_cast<int>(i) == selectedLightIndex)
						{
							nodeFlags |= ImGuiTreeNodeFlags_Selected;
						}

						if (!light.enabled)
						{
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
						}

						const char* icon = (light.type == LightType::Directional) ? "[D]" : "[P]";
						ImGui::TreeNodeEx((void*)(intptr_t)(1000 + i), nodeFlags, "%s %s", icon, light.name.c_str());

						if (ImGui::IsItemClicked())
						{
							m_EditorScene->SelectLight(static_cast<int>(i));
						}

						// Context menu
						if (ImGui::BeginPopupContextItem())
						{
							if (ImGui::MenuItem("Toggle Enabled"))
							{
								light.enabled = !light.enabled;
							}
							if (ImGui::MenuItem("Delete"))
							{
								m_EditorScene->RemoveLight(i);
								ImGui::EndPopup();
								if (!light.enabled) ImGui::PopStyleColor();
								break;
							}
							ImGui::EndPopup();
						}

						if (!light.enabled)
						{
							ImGui::PopStyleColor();
						}
					}

					ImGui::TreePop();
				}

				ImGui::TreePop();
			}

			ImGui::Separator();

			// Add object button
			if (ImGui::Button("+ Add Object"))
			{
				ImGui::OpenPopup("AddObjectPopup");
			}

			if (ImGui::BeginPopup("AddObjectPopup"))
			{
				if (ImGui::MenuItem("Load Model..."))
				{
					// TODO: Open file dialog
					LOG_INFO("Load model dialog - not yet implemented");
				}
				if (ImGui::MenuItem("Primitive Cube"))
				{
					// TODO: Add primitive cube
					LOG_INFO("Add cube - not yet implemented");
				}

				ImGui::Separator();
				if (ImGui::MenuItem("Directional Light"))
				{
					Light* newLight = m_EditorScene->AddLight("Directional Light", LightType::Directional);
					newLight->direction = glm::vec3(0.0f, -1.0f, 0.0f);
					newLight->color = glm::vec3(1.0f);
					newLight->intensity = 1.0f;
					LOG_INFO("Added directional light");
				}
				if (ImGui::MenuItem("Point Light"))
				{
					Light* newLight = m_EditorScene->AddLight("Point Light", LightType::Point);
					newLight->position = glm::vec3(0.0f, 3.0f, 0.0f);
					newLight->color = glm::vec3(1.0f, 0.8f, 0.6f);  // Warm
					newLight->intensity = 2.0f;
					LOG_INFO("Added point light");
				}

				ImGui::EndPopup();
			}

			ImGui::End();
		}

		// Helper method to apply a pipeline to the selected primitive
// Add this to the private section of EditorApplication
		void ApplyPipelineToSelectedPrimitive(PipelineType pipeline)
		{
			if (!m_EditorScene) return;

			SceneObject* selected = m_EditorScene->GetSelected();
			if (!selected || !selected->meshDrawable) return;

			// Get the renderer resources
			auto* vb = GetRenderer()->GetTestVertexBuffer();
			auto* ib = GetRenderer()->GetTestIndexBuffer();
			uint32_t ic = GetRenderer()->GetTestIndexCount();

			if (!vb || !ib || ic == 0) return;

			// Get current textures before we destroy the drawable
			ResourceManager* resources = GetRenderer()->GetResourceManager();

			// Create new drawable with the new pipeline
			auto newDrawable = std::make_unique<MeshDrawable>(vb, ib, ic, pipeline);

			// RESTORE THE TRANSFORM!
			newDrawable->SetTransform(selected->primitiveTransform);

			// Re-add a default texture
			if (resources)
			{
				const char* textureLookup[] = { "uv_checker", "default_white", "default_black", "default_normal" };
				int texIdx = selected->textureIndex;
				if (texIdx >= 0 && texIdx < 4)
				{
					VulkanTexture* texture = resources->GetTexture(textureLookup[texIdx]);
					if (texture)
					{
						newDrawable->AddTexture(texture);
					}
				}
			}

			selected->pipeline = pipeline;

			// Replace the drawable
			selected->meshDrawable = std::move(newDrawable);
		}

		void RenderInspector()
		{
			ImGui::Begin("Inspector", &m_ShowInspector);

			if (!m_EditorScene)
			{
				ImGui::Text("No scene");
				ImGui::End();
				return;
			}

			SceneObject* selected = m_EditorScene->GetSelected();

			if (!selected)
			{
				ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No object selected");
				ImGui::End();
				return;
			}

			// Object name (editable)
			char nameBuf[256];
			strncpy(nameBuf, selected->name.c_str(), sizeof(nameBuf) - 1);
			nameBuf[sizeof(nameBuf) - 1] = '\0';

			if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
			{
				selected->name = nameBuf;
			}

			// Visibility toggle
			ImGui::Checkbox("Visible", &selected->visible);

			ImGui::Separator();

			// Transform section
			if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (selected->model)
				{
					// Get current values
					glm::vec3 position = selected->GetPosition();
					glm::vec3 rotation = selected->GetRotation();
					glm::vec3 scale = selected->GetScale();

					// Convert rotation from radians to degrees for display
					glm::vec3 rotationDeg = glm::degrees(rotation);

					bool changed = false;

					// Position
					if (ImGui::DragFloat3("Position", &position.x, 0.1f))
					{
						selected->SetPosition(position);
						changed = true;
					}

					// Rotation (in degrees)
					if (ImGui::DragFloat3("Rotation", &rotationDeg.x, 1.0f))
					{
						selected->SetRotation(glm::radians(rotationDeg));
						changed = true;
					}

					// Scale
					if (ImGui::DragFloat3("Scale", &scale.x, 0.01f, 0.001f, 100.0f))
					{
						selected->SetScale(scale);
						changed = true;
					}

					// Uniform scale helper
					static float uniformScale = 1.0f;
					if (changed)
					{
						uniformScale = (scale.x + scale.y + scale.z) / 3.0f;
					}

					if (ImGui::DragFloat("Uniform Scale", &uniformScale, 0.01f, 0.001f, 100.0f))
					{
						selected->SetScale(uniformScale);
					}

					// Reset button
					if (ImGui::Button("Reset Transform"))
					{
						selected->SetPosition(glm::vec3(0.0f));
						selected->SetRotation(glm::vec3(0.0f));
						selected->SetScale(glm::vec3(1.0f));
					}
				}
				else
				{
					ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
						"(Primitive - transform not editable yet)");
				}
			}

			// Mesh info section
			if (selected->model && ImGui::CollapsingHeader("Mesh Info"))
			{
				ImGui::Text("Meshes: %zu", selected->GetMeshCount());
				ImGui::Text("Vertices: %zu", selected->GetVertexCount());
				ImGui::Text("Indices: %zu", selected->GetIndexCount());

				// List individual meshes
				if (ImGui::TreeNode("Meshes"))
				{
					const auto& meshes = selected->model->GetMeshes();
					for (size_t i = 0; i < meshes.size(); ++i)
					{
						const auto& mesh = meshes[i];
						ImGui::BulletText("%s: %u verts, %u indices",
							mesh->GetName().c_str(),
							mesh->GetVertexCount(),
							mesh->GetIndexCount());
					}
					ImGui::TreePop();
				}
			}

			// Materials section
			if (selected->model && ImGui::CollapsingHeader("Materials"))
			{
				const auto& materials = selected->model->GetMaterials();

				if (materials.empty())
				{
					ImGui::Text("No materials");
				}
				else
				{
					for (size_t i = 0; i < materials.size(); ++i)
					{
						Material* mat = materials[i].get();
						if (!mat) continue;

						ImGui::PushID(static_cast<int>(i));

						if (ImGui::TreeNode(mat->GetName().c_str()))
						{
							// Albedo color
							glm::vec4 albedo = mat->GetAlbedoColor();
							if (ImGui::ColorEdit4("Albedo", &albedo.x))
							{
								mat->SetAlbedoColor(albedo);
							}

							// Metallic/Roughness
							float metallic = mat->GetMetallic();
							if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f))
							{
								mat->SetMetallic(metallic);
							}

							float roughness = mat->GetRoughness();
							if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f))
							{
								mat->SetRoughness(roughness);
							}

							// Texture info
							if (mat->HasAlbedoTexture())
							{
								ImGui::Text("Albedo Texture: Yes");
							}
							else
							{
								ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Albedo Texture: None");
							}

							if (mat->HasNormalTexture())
							{
								ImGui::Text("Normal Texture: Yes");
							}

							ImGui::TreePop();
						}

						ImGui::PopID();
					}
				}
			}

			ImGui::End();
		}

		void RenderLightingPanel()
		{
			Light* selectedLight = m_EditorScene ? m_EditorScene->GetSelectedLight() : nullptr;

			if (!selectedLight)
			{
				// No light selected — show summary
				if (m_EditorScene)
				{
					ImGui::Text("Lights: %zu", m_EditorScene->GetLightCount());
					ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
						"Select a light in the hierarchy to edit");
				}
				return;
			}

			// ---- Name ----
			char nameBuf[256];
			strncpy(nameBuf, selectedLight->name.c_str(), sizeof(nameBuf) - 1);
			nameBuf[sizeof(nameBuf) - 1] = '\0';
			if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
			{
				selectedLight->name = nameBuf;
			}

			// ---- Enabled ----
			ImGui::Checkbox("Enabled", &selectedLight->enabled);

			// ---- Type ----
			const char* typeNames[] = { "Directional", "Point" };
			int currentType = static_cast<int>(selectedLight->type);
			if (ImGui::Combo("Type", &currentType, typeNames, 2))
			{
				selectedLight->type = static_cast<LightType>(currentType);
			}

			ImGui::Separator();

			// ---- Color + Intensity ----
			ImGui::ColorEdit3("Color", &selectedLight->color.x);
			ImGui::SliderFloat("Intensity", &selectedLight->intensity, 0.0f, 10.0f, "%.2f");

			ImGui::Separator();

			// ---- Type-specific properties ----
			if (selectedLight->type == LightType::Directional)
			{
				ImGui::Text("Direction");
				ImGui::DragFloat3("Dir", &selectedLight->direction.x, 0.01f, -1.0f, 1.0f);

				// Normalize button
				if (ImGui::Button("Normalize Direction"))
				{
					float len = glm::length(selectedLight->direction);
					if (len > 0.0001f)
					{
						selectedLight->direction = glm::normalize(selectedLight->direction);
					}
				}

				// Visual helper: show direction as angles
				glm::vec3 dir = glm::normalize(selectedLight->direction);
				float elevation = glm::degrees(asinf(-dir.y));  // Negative because direction is FROM light
				float azimuth = glm::degrees(atan2f(dir.x, dir.z));
				ImGui::Text("Elevation: %.1f deg, Azimuth: %.1f deg", elevation, azimuth);
			}
			else if (selectedLight->type == LightType::Point)
			{
				ImGui::DragFloat3("Position", &selectedLight->position.x, 0.1f);
				ImGui::SliderFloat("Radius", &selectedLight->radius, 1.0f, 200.0f);

				if (ImGui::CollapsingHeader("Attenuation"))
				{
					ImGui::SliderFloat("Constant", &selectedLight->constant, 0.0f, 5.0f);
					ImGui::SliderFloat("Linear", &selectedLight->linear, 0.0f, 1.0f, "%.4f");
					ImGui::SliderFloat("Quadratic", &selectedLight->quadratic, 0.0f, 0.5f, "%.5f");

					// Presets
					if (ImGui::Button("Short Range"))
					{
						selectedLight->constant = 1.0f;
						selectedLight->linear = 0.35f;
						selectedLight->quadratic = 0.44f;
						selectedLight->radius = 10.0f;
					}
					ImGui::SameLine();
					if (ImGui::Button("Medium Range"))
					{
						selectedLight->constant = 1.0f;
						selectedLight->linear = 0.09f;
						selectedLight->quadratic = 0.032f;
						selectedLight->radius = 50.0f;
					}
					ImGui::SameLine();
					if (ImGui::Button("Long Range"))
					{
						selectedLight->constant = 1.0f;
						selectedLight->linear = 0.022f;
						selectedLight->quadratic = 0.0019f;
						selectedLight->radius = 150.0f;
					}
				}
			}

			// ---- Ambient (scene-wide, show for any light) ----
			ImGui::Separator();
			ImGui::Text("Scene Ambient");

			// Access the scene's ambient through the first light's context
			// (ambient is scene-wide, stored in SceneLightingData)

			glm::vec3 ambientColor = m_EditorScene->GetAmbientColor();
			float ambientIntensity = m_EditorScene->GetAmbientIntensity();

			if (ImGui::ColorEdit3("Ambient Color", &ambientColor.x))
			{
				m_EditorScene->SetAmbient(ambientColor, ambientIntensity);
			}
			if (ImGui::SliderFloat("Ambient Intensity", &ambientIntensity, 0.0f, 2.0f))
			{
				m_EditorScene->SetAmbient(ambientColor, ambientIntensity);
			}

			ImGui::Separator();
			ImGui::Text("Shadow Settings");

			Renderer* renderer = GetRenderer();
			bool shadowEnabled = renderer->IsShadowEnabled();
			if (ImGui::Checkbox("Enable Shadows", &shadowEnabled))
			{
				renderer->SetShadowEnabled(shadowEnabled);
			}

			// Shadow center control
			static glm::vec3 shadowCenter = glm::vec3(0.0f);
			if (ImGui::DragFloat3("Shadow Center", &shadowCenter.x, 0.5f))
			{
				renderer->SetShadowCenter(shadowCenter);
			}
		}

		void RenderConsole()
		{
			ImGui::Begin("Console", &m_ShowConsole);

			ImGui::Text("Console output:");
			ImGui::Separator();

			// Simple log display
			ImGui::BeginChild("LogScroll", ImVec2(0, -25), true);
			ImGui::Text("[INFO] Editor started");
			ImGui::Text("[INFO] Renderer initialized");
			ImGui::EndChild();

			// Command input
			static char inputBuf[256] = "";
			if (ImGui::InputText("Command", inputBuf, sizeof(inputBuf),
				ImGuiInputTextFlags_EnterReturnsTrue))
			{
				LOG_INFO("Console command: {}", inputBuf);
				inputBuf[0] = '\0';
			}

			ImGui::End();
		}

		void RenderAssetBrowser()
		{
			ImGui::Begin("Asset Browser", &m_ShowAssetBrowser);

			if (ImGui::Button("Import Asset..."))
			{
				// Open file dialog
			}

			ImGui::Separator();

			// Asset tree
			if (ImGui::TreeNode("Shaders"))
			{
				ImGui::Text("triangle.vert");
				ImGui::Text("triangle.frag");
				ImGui::Text("mesh.vert");
				ImGui::Text("mesh.frag");
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Textures"))
			{
				ImGui::Text("No textures");
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Models"))
			{
				ImGui::Text("No models");
				ImGui::TreePop();
			}

			ImGui::End();
		}

		void RenderSceneViewport()
		{
			ImGui::Begin("Viewport");

			// Viewport controls
			if (ImGui::Button("Perspective")) {}
			ImGui::SameLine();
			if (ImGui::Button("Top")) {}
			ImGui::SameLine();
			if (ImGui::Button("Side")) {}

			ImGui::Separator();

			// Add pipeline toggle button
			if (ImGui::Button("Toggle Pipeline (P)")) {
				if (GetRenderer()) GetRenderer()->TogglePipeline();
			}
			ImGui::SameLine();
			if (ImGui::Button("Reload Shaders (R)")) {
				if (GetRenderer()) GetRenderer()->ReloadShaders();
			}

			ImGui::Separator();

			// Viewport info
			ImVec2 viewportSize = ImGui::GetContentRegionAvail();
			ImGui::Text("3D Viewport (%dx%d)", (int)viewportSize.x, (int)viewportSize.y);
			ImGui::Text("Current Project: %s", m_CurrentProjectName.c_str());
			
			if (m_IsPlayMode)
			{
				ImGui::TextColored(ImVec4(0, 1, 0, 1), "PLAYING");
			}

			ImGui::End();
		}

		void HandleEditorShortcuts()
		{
			if (!GetInput()) return;

			bool ctrl = GetInput()->IsDown(InputCode::Key_Control);
				//|| GetInput()->IsDown(InputCode::Key_RightControl);

			// File shortcuts
			if (ctrl && GetInput()->IsPressed(InputCode::Key_N)) NewScene();
			if (ctrl && GetInput()->IsPressed(InputCode::Key_O)) OpenScene();
			if (ctrl && GetInput()->IsPressed(InputCode::Key_S)) SaveScene();

			// Play mode
			if (GetInput()->IsPressed(InputCode::Key_F5)) TogglePlayMode();
			if (GetInput()->IsPressed(InputCode::Key_F7)) StopPlayMode();

			// Tools
			if (ctrl && GetInput()->IsPressed(InputCode::Key_R))
			{
				if (GetRenderer()) GetRenderer()->ReloadShaders();
			}

			// Pipeline toggle
			if (GetInput()->IsPressed(InputCode::Key_P))
			{
				if (GetRenderer()) GetRenderer()->TogglePipeline();
			}

			// Exit
			if (GetInput()->IsPressed(InputCode::Key_Escape))
			{
				LOG_INFO("Escape pressed - exiting editor");
				Quit();
			}
		}

		// Editor operations
		// Project management
		void NewProject()
		{
			LOG_INFO("Creating new project");
			// TODO: Project creation wizard
		}

		void OpenProject()
		{
			LOG_INFO("Opening project");
			// TODO: Project browser dialog
		}

		void SaveProject()
		{
			LOG_INFO("Saving project: {}", m_CurrentProjectName);
			// TODO: Save project settings
		}

		// Scene management
		void NewScene()
		{
			LOG_INFO("Creating new scene");
		}

		void OpenScene()
		{
			LOG_INFO("Opening scene");
		}

		void SaveScene()
		{
			LOG_INFO("Saving scene");
		}

		void SaveSceneAs()
		{
			LOG_INFO("Save scene as");
		}

		void TogglePlayMode()
		{
			m_IsPlayMode = !m_IsPlayMode;
			LOG_INFO(m_IsPlayMode ? "Entering play mode" : "Exiting play mode");
		}

		void StopPlayMode()
		{
			if (m_IsPlayMode)
			{
				m_IsPlayMode = false;
				LOG_INFO("Stopping play mode");
			}
		}

		void CompileAllShaders()
		{
			LOG_INFO("Compiling all shaders in project");
			// TODO: Batch compile all shaders
		}

		bool CompileAndApplyNodeShaders()
		{
			if (!m_ShaderNodeEditor)
			{
				LOG_ERROR("Shader node editor not initialized");
				m_CompileError = true;
				m_LastError = "Shader node editor not initialized";
				return false;
			}

			// Generate shader code from the node graph
			if (!m_ShaderNodeEditor->CompileShaders())
			{
				LOG_ERROR("Failed to generate shader code from nodes");
				m_CompileSuccess = false;
				m_CompileError = true;
				m_LastError = m_ShaderNodeEditor->GetLastError();
				return false;
			}

			std::string vertCode = m_ShaderNodeEditor->GetVertexShader();
			std::string fragCode = m_ShaderNodeEditor->GetFragmentShader();

			// Save and compile GLSL to SPIR-V
			bool vertSuccess = Editor::EditorFileUtils::SaveShaderFile("NodeGenerated.vert", vertCode);
			bool fragSuccess = Editor::EditorFileUtils::SaveShaderFile("NodeGenerated.frag", fragCode);

			if (!vertSuccess || !fragSuccess)
			{
				LOG_ERROR("Failed to compile shaders to SPIR-V");
				m_CompileSuccess = false;
				m_CompileError = true;
				m_LastError = "Failed to compile shaders to SPIR-V";
				return false;
			}

			// Get the resource manager from renderer
			ResourceManager* resources = GetRenderer()->GetResourceManager();
			if (!resources)
			{
				LOG_ERROR("Resource manager not available");
				m_CompileSuccess = false;
				m_CompileError = true;
				m_LastError = "Resource manager not available";
				return false;
			}

			// Remove old node shaders if they exist
			resources->DestroyShader("node_vert");
			resources->DestroyShader("node_frag");

			// Load the newly compiled shaders
			VulkanShader* vertShader = resources->LoadShader(
				"node_vert",
				ShaderStage::Vertex,
				"NodeGenerated.vert.spv"
			);

			VulkanShader* fragShader = resources->LoadShader(
				"node_frag",
				ShaderStage::Fragment,
				"NodeGenerated.frag.spv"
			);

			if (!vertShader || !fragShader)
			{
				LOG_ERROR("Failed to load compiled node shaders");
				m_CompileSuccess = false;
				m_CompileError = true;
				m_LastError = "Failed to load compiled shaders into resource manager";
				return false;
			}

			// Now create the pipeline using the loaded shaders
			// We need to get the Vulkan pipeline manager directly
			//VulkanPipelineAdapter* adapter = static_cast<VulkanPipelineAdapter*>(
			//	GetRenderer()->GetPipelineManager()
			//	);
			//VulkanPipelineManager* vkPipelineManager = adapter->GetVulkanManager();

			// Create pipeline config with shader objects
	// Create the NodeGenerated pipeline
			//VulkanPipelineAdapter* adapter = static_cast<VulkanPipelineAdapter*>(
			//	GetRenderer()->GetPipelineManager()
			//	);
			//VulkanPipelineManager* vkPipelineManager = adapter->GetVulkanManager();

			PipelineConfig config;
			config.vertexShader = vertShader;
			config.fragmentShader = fragShader;
			config.useVertexInput = true;
			config.topology = PrimitiveTopology::TriangleList;
			config.polygonMode = PolygonMode::Fill;
			config.cullMode = CullMode::Back;
			config.frontFace = FrontFace::CounterClockwise;
			config.depthTestEnable = true;
			config.depthWriteEnable = true;
			config.depthCompareOp = CompareOp::GreaterOrEqual;
			config.useTextures = m_ShaderNodeEditor->UsesTextures();
			config.useUniformBuffer = true;
			config.pushConstantSize = sizeof(PushConstantData);
			config.pushConstantStages = ShaderStage::VertexFragment;

			if (GetRenderer()->GetPipelineManager()->CreatePipeline(PipelineType::NodeGenerated, config))
			{
				LOG_INFO("Successfully created NodeGenerated pipeline");

				// Switch to the new pipeline
				m_CurrentTestPipeline = PipelineType::NodeGenerated;
				//UpdateTestCube();  // Your existing method to update the cube

				return true;
			}

			LOG_ERROR("Failed to create NodeGenerated pipeline");
			return false;
		}

		// Help
		void OpenDocumentation()
		{
			LOG_INFO("Opening documentation");
			// TODO: Open web browser to docs
		}

		void ShowAboutDialog()
		{
			LOG_INFO("Nightbloom Editor v0.1.0");
		}

		void LoadEditorSettings()
		{
			// TODO: Load from config file
		}

		void SaveEditorSettings()
		{
			// TODO: Save to config file
		}
	};

} // namespace Nightbloom

// Entry point
Nightbloom::Application* Nightbloom::CreateApplication()
{
	return new Nightbloom::EditorApplication();
}