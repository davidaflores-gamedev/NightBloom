//------------------------------------------------------------------------------
// Renderer.cpp (REFACTORED VERSION)
//
// Simplified renderer implementation using components
//------------------------------------------------------------------------------

#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/RenderDevice.hpp"
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanPipelineAdapter.hpp"
#include "Engine/Renderer/Vulkan/VulkanShader.hpp"

//Components
#include "Engine/Renderer/Components/FrameSyncManager.hpp"
#include "Engine/Renderer/Components/RenderPassManager.hpp"
#include "Engine/Renderer/Components/CommandRecorder.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Components/ShadowMapManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include "Engine/Renderer/Components/UIManager.hpp"
#include "Engine/Renderer/AssetManager.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include "Engine/Core/PerformanceMetrics.hpp"
#include <filesystem>


namespace Nightbloom
{
	Renderer::Renderer()
	{
		LOG_INFO("Renderer created");
	}

	Renderer::~Renderer()
	{
		if (m_Initialized)
		{
			Shutdown();
		}
		LOG_INFO("Renderer destroyed");
	}

	bool Renderer::Initialize(void* windowHandle, uint32_t width, uint32_t height)
	{
		LOG_INFO("=== Initializing Renderer ===");
		LOG_INFO("Window: {}x{}", width, height);

		m_WindowHandle = windowHandle;
		m_Width = width;
		m_Height = height;

		// Initialize performance tracking
		PerformanceMetrics::Get().Reset();
		PerformanceMetrics::Get().BeginFrame();

		// Initialize core systems
		if (!InitializeCore())
		{
			LOG_ERROR("Failed to initialize core systems");
			return false;
		}

		// Initialize components
		if (!InitializeComponents())
		{
			LOG_ERROR("Failed to initialize components");
			return false;
		}

		// Initialize pipelines
		if (!InitializePipelines())
		{
			LOG_ERROR("Failed to initialize pipelines");
			return false;
		}

		// Initialize shadow mapping
		if (!InitializeShadowMapping())
		{
			LOG_WARN("Failed to initialize shadow mapping - continuing without shadows");
			m_ShadowEnabled = false;
		}

		m_Initialized = true;

		// End initialization timing
		PerformanceMetrics::Get().EndFrame();

		// Log memory stats
		if (m_MemoryManager)
		{
			m_MemoryManager->LogMemoryStats();
		}

		LOG_INFO("=== Renderer Initialization Complete ===");
		PerformanceMetrics::Get().LogMetrics();

		return true;
	}

	void Renderer::Shutdown()
	{
		if (!m_Initialized)
		{
			LOG_WARN("Renderer is not initialized");
			return;
		}

		LOG_INFO("=== Shutting down Renderer ===");

		// Wait for device to be idle
		if (m_Device)
		{
			m_Device->WaitForIdle();
		}

		// Log final performance metrics
		PerformanceMetrics::Get().LogMetrics();

		// Cleanup components in reverse order of initialization
		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());

		if (m_ShadowManager)
		{
			m_ShadowManager->Cleanup();
			m_ShadowManager.reset();
		}

		if (m_UI)
		{
			m_UI->Cleanup(vkDevice->GetDevice());
			m_UI.reset();
		}

		if (m_Commands)
		{
			m_Commands->Cleanup();
			m_Commands.reset();
		}

		if (m_DescriptorManager)
		{
			m_DescriptorManager->Cleanup();
			m_DescriptorManager.reset();
		}

		if (m_Resources)
		{
			m_Resources->Cleanup();
			m_Resources.reset();
		}

		if (m_RenderPasses)
		{
			m_RenderPasses->Cleanup(vkDevice->GetDevice());
			m_RenderPasses.reset();
		}

		if (m_FrameSync)
		{
			m_FrameSync->Cleanup(vkDevice->GetDevice());
			m_FrameSync.reset();
		}

		// Cleanup core systems
		m_PipelineAdapter.reset();
		m_Swapchain.reset();

		// Log final memory stats
		if (m_MemoryManager)
		{
			m_MemoryManager->DestroyStagingPool();

			LOG_INFO("=== Final Memory Statistics ===");
			m_MemoryManager->LogMemoryStats();

			auto stats = m_MemoryManager->GetMemoryStats();
			if (stats.allocationCount > 0)
			{
				LOG_WARN("Warning: {} allocations still active at shutdown!", stats.allocationCount);
			}
		}

		m_MemoryManager.reset();
		m_Device.reset();

		// Clean up AssetManager
		AssetManager::Get().Shutdown();

		m_Initialized = false;
		LOG_INFO("=== Renderer Shutdown Complete ===");
	}

	void Renderer::BeginFrame()
	{
		m_FrameValid = false;

		if (!m_Initialized)
		{
			LOG_ERROR("Renderer not initialized");
			return;
		}

		// Start frame timing
		PerformanceMetrics::Get().BeginFrame();

		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());

		// Wait for previous frame
		if (!m_FrameSync->WaitForFrame(vkDevice->GetDevice()))
		{
			LOG_ERROR("Failed to wait for frame");
			return;
		}

		// Acquire next image
		if (!m_FrameSync->AcquireNextImage(vkDevice->GetDevice(), m_Swapchain.get(), m_CurrentImageIndex))
		{
			LOG_WARN("Failed to acquire image - swapchain may need recreation");
			HandleSwapchainResize();
			return;
		}

		// Track time for shaders
		static auto startTime = std::chrono::high_resolution_clock::now();
		auto currentTime = std::chrono::high_resolution_clock::now();
		m_TotalTime = std::chrono::duration<float>(currentTime - startTime).count();

		uint32_t frameIndex = m_FrameSync->GetCurrentFrame();

		// =====================================================================
		// FIX: Compute shadow matrices FIRST so m_ShadowFrameData is populated
		// before we upload it to the GPU buffer below.
		// Previously this was called AFTER the upload, meaning the shadow UBO
		// always contained stale data from the previous frame (or zeros).
		// =====================================================================
		UpdateShadowMatrices();

		// Update camera uniform buffer (set 0 in main pass)
		m_CurrentFrameData.view = m_ViewMatrix;
		m_CurrentFrameData.proj = m_ProjectionMatrix;
		m_CurrentFrameData.time.x = m_TotalTime;
		m_CurrentFrameData.cameraPos = glm::vec4(m_CameraPosition, 1.0f);

		void* mapped = m_FrameUniforms[frameIndex]->GetPersistentMappedPtr();
		if (mapped)
		{
			memcpy(mapped, &m_CurrentFrameData, sizeof(FrameUniformData));
			m_FrameUniforms[frameIndex]->Flush();
		}

		// Upload shadow uniform buffer (set 0 in shadow pass - light's view/proj)
		// Now happens AFTER UpdateShadowMatrices has populated m_ShadowFrameData
		void* shadowMapped = m_ShadowUniforms[frameIndex]->GetPersistentMappedPtr();
		if (shadowMapped)
		{
			memcpy(shadowMapped, &m_ShadowFrameData, sizeof(FrameUniformData));
			m_ShadowUniforms[frameIndex]->Flush();
		}

		// Upload lighting UBO (set 2)
		void* lightMapped = m_LightingUniforms[frameIndex]->GetPersistentMappedPtr();
		if (lightMapped)
		{
			memcpy(lightMapped, &m_CurrentLightingData, sizeof(SceneLightingData));
			m_LightingUniforms[frameIndex]->Flush();
		}

		// Clear draw list for new frame
		m_FrameDrawList.Clear();

		// Start GPU timing
		PerformanceMetrics::Get().BeginGPUWork();

		// Begin ImGui frame
		if (m_UI)
		{
			m_UI->BeginFrame();
		}

		m_FrameValid = true;
	}

	void Renderer::EndFrame()
	{
		if (!m_Initialized) return;

		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());
		uint32_t frameIndex = m_FrameSync->GetCurrentFrame();

		// Submit command buffer
		VkCommandBuffer cmd = m_Commands->GetCommandBuffer(frameIndex);
		if (!m_FrameSync->SubmitCommandBuffer(vkDevice->GetDevice(),
			vkDevice->GetGraphicsQueue(),
			cmd, m_CurrentImageIndex))
		{
			LOG_ERROR("Failed to submit command buffer");
		}

		// End GPU timing
		PerformanceMetrics::Get().EndGPUWork();

		// Present
		if (!m_FrameSync->PresentImage(m_Swapchain.get(),
			vkDevice->GetPresentQueue(),
			m_CurrentImageIndex))
		{
			LOG_WARN("Failed to present - swapchain may need recreation");
			HandleSwapchainResize();
		}

		static int gcCounter = 0;
		if (++gcCounter >= 300)
		{
			if (auto* pool = m_MemoryManager->GetStagingPool())
			{
				pool->GarbageCollect();
			}
			gcCounter = 0;
		}

		// Update memory stats periodically (every 60 frames)
		static int frameCounter = 0;
		if (++frameCounter % 60 == 0)
		{
			auto memStats = m_MemoryManager->GetMemoryStats();
			PerformanceMetrics::Get().UpdateMemoryStats(
				memStats.totalAllocatedBytes,
				memStats.totalUsedBytes
			);
		}

		// End frame timing
		PerformanceMetrics::Get().EndFrame();

		// Log metrics every second
		static int logCounter = 0;
		if (++logCounter >= 60)
		{
			PerformanceMetrics::Get().LogMetrics();
			logCounter = 0;
		}
	}

	void Renderer::FinalizeFrame()
	{
		if (!m_Initialized) return;

		// Finalize ImGui
		if (m_UI)
		{
			m_UI->EndFrame();
		}

		// Record command buffer with all draw commands
		uint32_t frameIndex = m_FrameSync->GetCurrentFrame();
		RecordCommandBuffer(frameIndex, m_CurrentImageIndex);
	}

	void Renderer::SubmitDrawList(const DrawList& drawList)
	{
		m_FrameDrawList = drawList;
	}

	void Renderer::Clear(float r, float g, float b, float a)
	{
		m_ClearColor = glm::vec4(r, g, b, a);
	}

	Buffer* Renderer::GetTestVertexBuffer() const
	{
		if (!m_Resources) return nullptr;
		return m_Resources->GetTestVertexBuffer();
	}

	Buffer* Renderer::GetTestIndexBuffer() const
	{
		if (!m_Resources) return nullptr;
		return m_Resources->GetTestIndexBuffer();
	}

	uint32_t Renderer::GetTestIndexCount() const
	{
		if (!m_Resources) return 0;
		return m_Resources->GetTestIndexCount();
	}

	Buffer* Renderer::GetGroundPlaneVertexBuffer() const
	{
		if (!m_Resources) return nullptr;
		return m_Resources->GetGroundPlaneVertexBuffer();
	}

	Buffer* Renderer::GetGroundPlaneIndexBuffer() const
	{
		if (!m_Resources) return nullptr;
		return m_Resources->GetGroundPlaneIndexBuffer();
	}

	uint32_t Renderer::GetGroundPlaneIndexCount() const
	{
		if (!m_Resources) return 0;
		return m_Resources->GetGroundPlaneIndexCount();
	}

	void Renderer::TestShaderClass()
	{
		// Load a shader file
		auto shaderCode = AssetManager::Get().LoadShaderBinary("triangle.vert");

		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());

		// Create a VulkanShader
		VulkanShader testShader(vkDevice, ShaderStage::Vertex);

		if (testShader.CreateFromSpirV(shaderCode))
		{
			LOG_INFO("Test shader created successfully!");

			// Get the stage info
			auto stageInfo = testShader.GetStageInfo();
			LOG_INFO("Stage info created, entry point: {}", testShader.GetEntryPoint());
		}
		else
		{
			LOG_ERROR("Failed to create test shader");
		}
	}

	bool Renderer::LoadShaders()
	{
		LOG_INFO("=== Loading Shaders ===");

		// Load triangle shaders
		if (!m_Resources->LoadShader("triangle_vert", ShaderStage::Vertex, "triangle.vert"))
		{
			LOG_ERROR("Failed to load triangle vertex shader");
			return false;
		}

		if (!m_Resources->LoadShader("triangle_frag", ShaderStage::Fragment, "triangle.frag"))
		{
			LOG_ERROR("Failed to load triangle fragment shader");
			return false;
		}

		// Load mesh shaders
		if (!m_Resources->LoadShader("mesh_vert", ShaderStage::Vertex, "Mesh.vert"))
		{
			LOG_WARN("Failed to load mesh vertex shader - continuing without mesh pipeline");
		}

		if (!m_Resources->LoadShader("mesh_frag", ShaderStage::Fragment, "Mesh.frag"))
		{
			LOG_WARN("Failed to load mesh fragment shader - continuing without mesh pipeline");
		}

		LOG_INFO("Shaders loaded successfully");
		return true;
	}

	void Renderer::TogglePipeline()
	{
		if (!m_PipelineAdapter)
		{
			LOG_WARN("Pipeline adapter not initialized");
			return;
		}

		// Wait for device to be idle
		m_Device->WaitForIdle();

		// Toggle between Triangle and Mesh pipelines
		if (m_CurrentPipeline == PipelineType::Triangle)
		{
			if (m_PipelineAdapter->GetPipeline(PipelineType::Mesh))
			{
				m_CurrentPipeline = PipelineType::Mesh;
				LOG_INFO("Switched to Mesh pipeline");
			}
			else
			{
				LOG_WARN("Mesh pipeline not available");
			}
		}
		else
		{
			m_CurrentPipeline = PipelineType::Triangle;
			LOG_INFO("Switched to Triangle pipeline");
		}
	}

	void Renderer::ReloadShaders()
	{
		if (!m_PipelineAdapter)
		{
			LOG_WARN("Pipeline adapter not initialized");
			return;
		}

		LOG_INFO("Reloading all shaders...");

		m_Device->WaitForIdle();

		if (m_PipelineAdapter->ReloadAllPipelines())
		{
			LOG_INFO("Shaders reloaded successfully");
		}
		else
		{
			LOG_ERROR("Failed to reload shaders");
		}
	}

	bool Renderer::InitializeCore()
	{
		// Initialize AssetManager
		LOG_INFO("=== Initializing Asset Manager ===");
		std::filesystem::path execPath = std::filesystem::current_path();
		if (!AssetManager::Get().Initialize(execPath.string()))
		{
			LOG_ERROR("Failed to initialize AssetManager");
			return false;
		}

		// Create Vulkan device
		LOG_INFO("=== Creating VulkanDevice ===");
		m_Device = std::make_unique<VulkanDevice>();

		if (!m_Device->Initialize(m_WindowHandle, m_Width, m_Height))
		{
			LOG_ERROR("Failed to initialize VulkanDevice");
			return false;
		}

		// Display device capabilities
		LOG_INFO("=== Device Capabilities ===");
		LOG_INFO("Min Uniform Buffer Alignment: {} bytes",
			m_Device->GetMinUniformBufferAlignment());

		// Initialize memory manager
		LOG_INFO("=== Creating VulkanMemoryManager ===");
		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());
		m_MemoryManager = std::make_unique<VulkanMemoryManager>(vkDevice);

		if (!m_MemoryManager->Initialize())
		{
			LOG_ERROR("Failed to initialize memory manager");
			return false;
		}

		// Initialize swapchain
		LOG_INFO("=== Creating VulkanSwapchain ===");
		m_Swapchain = std::make_unique<VulkanSwapchain>(vkDevice);

		if (!m_Swapchain->Initialize(m_WindowHandle, m_Width, m_Height))
		{
			LOG_ERROR("Failed to initialize swapchain");
			return false;
		}

		return true;
	}

	bool Renderer::InitializeComponents()
	{
		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());

		// Initialize frame synchronization
		m_FrameSync = std::make_unique<FrameSyncManager>();
		if (!m_FrameSync->Initialize(vkDevice->GetDevice(), m_Swapchain->GetImages().size()))
		{
			LOG_ERROR("Failed to initialize frame synchronization");
			return false;
		}

		// Initialize render passes
		m_RenderPasses = std::make_unique<RenderPassManager>();
		if (!m_RenderPasses->Initialize(vkDevice->GetDevice(), m_Swapchain.get(), m_MemoryManager.get()))
		{
			LOG_ERROR("Failed to initialize render passes");
			return false;
		}

		// Initialize resources
		m_Resources = std::make_unique<ResourceManager>();
		if (!m_Resources->Initialize(vkDevice, m_MemoryManager.get()))
		{
			LOG_ERROR("Failed to initialize resource manager");
			return false;
		}

		// Initialize descriptor manager AFTER device, BEFORE pipelines
		m_DescriptorManager = std::make_unique<VulkanDescriptorManager>(vkDevice);
		if (!m_DescriptorManager->Initialize())
		{
			LOG_ERROR("Failed to initialize descriptor manager");
			return false;
		}

		// TODO: consider changing this to be part of the initialize function for resourcemanager
		m_Resources->SetDescriptorManager(m_DescriptorManager.get());

		// Create uniform buffers for each frame in flight
		LOG_INFO("Creating frame uniform buffers");
		for (uint32_t i = 0; i < 2; ++i)  // 2 = MAX_FRAMES_IN_FLIGHT
		{
			std::string bufferName = "FrameUniform_" + std::to_string(i);
			m_FrameUniforms[i] = m_Resources->CreateUniformBuffer(
				bufferName,
				sizeof(FrameUniformData)
			);

			if (!m_FrameUniforms[i])
			{
				LOG_ERROR("Failed to create uniform buffer for frame {}", i);
				return false;
			}

			// Update descriptor set with this buffer
			m_DescriptorManager->UpdateUniformSet(i,
				m_FrameUniforms[i]->GetBuffer(),
				sizeof(FrameUniformData));
		}
		LOG_INFO("Frame uniform buffers created");

		// Create lighting uniform buffers for each frame in flight
		LOG_INFO("Creating lighting uniform buffers");
		for (uint32_t i = 0; i < 2; ++i)
		{
			std::string bufferName = "LightingUniform_" + std::to_string(i);
			m_LightingUniforms[i] = m_Resources->CreateUniformBuffer(
				bufferName,
				sizeof(SceneLightingData)
			);

			if (!m_LightingUniforms[i])
			{
				LOG_ERROR("Failed to create lighting uniform buffer for frame {}", i);
				return false;
			}

			// Update descriptor set with this buffer
			m_DescriptorManager->UpdateLightingSet(i,
				m_LightingUniforms[i]->GetBuffer(),
				sizeof(SceneLightingData));
		}
		LOG_INFO("Lighting uniform buffers created");

		// =================================================================
		// FIX: Create shadow uniform buffers AND point the shadow uniform
		// descriptor sets at them so the shadow pass binds the light's
		// view/proj instead of the camera's.
		// =================================================================
		LOG_INFO("Creating shadow uniform buffers");
		for (uint32_t i = 0; i < 2; ++i)
		{
			std::string bufferName = "ShadowUniform_" + std::to_string(i);
			m_ShadowUniforms[i] = m_Resources->CreateUniformBuffer(
				bufferName, sizeof(FrameUniformData));

			if (!m_ShadowUniforms[i])
			{
				LOG_ERROR("Failed to create shadow uniform buffer for frame {}", i);
				return false;
			}

			// Point the shadow uniform descriptor set at this buffer
			m_DescriptorManager->UpdateShadowUniformSet(i,
				m_ShadowUniforms[i]->GetBuffer(),
				sizeof(FrameUniformData));
		}
		LOG_INFO("Shadow uniform buffers created");

		// Create test geometry
		if (!m_Resources->CreateTestCube())
		{
			LOG_ERROR("Failed to create test geometry");
			return false;
		}

		if (!m_Resources->CreateGroundPlane(200.0f, 10.0f))
		{
			LOG_WARN("Failed to create ground plane");
			// Non-fatal — continue without it
		}

		// Create test Textures
		if (!m_Resources->CreateDefaultTextures())
		{
			LOG_WARN("Failed to create default textures");
		}


		// Initialize command recorder
		m_Commands = std::make_unique<CommandRecorder>();
		if (!m_Commands->Initialize(vkDevice, m_DescriptorManager.get(), FrameSyncManager::MAX_FRAMES_IN_FLIGHT))
		{
			LOG_ERROR("Failed to initialize command recorder");
			return false;
		}

		// Initialize UI (optional)
		m_UI = std::make_unique<UIManager>();
		if (!m_UI->Initialize(vkDevice, m_WindowHandle,
			m_RenderPasses->GetMainRenderPass(),
			m_Swapchain->GetImageCount()))
		{
			LOG_WARN("Failed to initialize UI manager - continuing without UI");
			m_UI.reset();
		}

		return true;
	}

	bool Renderer::InitializePipelines()
	{
		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());

		LOG_INFO("=== Creating Pipeline Manager ===");
		m_PipelineAdapter = std::make_unique<VulkanPipelineAdapter>();

		if (!m_PipelineAdapter->Initialize(vkDevice->GetDevice(),
			m_RenderPasses->GetMainRenderPass(),
			m_Swapchain->GetExtent(),
			m_DescriptorManager.get()))
		{
			LOG_ERROR("Failed to initialize pipeline adapter");
			return false;
		}

		// LOAD SHADERS FIRST!
		if (!LoadShaders())
		{
			LOG_ERROR("Failed to load shaders");
			return false;
		}

		// Create triangle pipeline using shader objects
		{
			PipelineConfig config;
			config.vertexShader = m_Resources->GetShader("triangle_vert");
			config.fragmentShader = m_Resources->GetShader("triangle_frag");
			config.useVertexInput = true;
			config.topology = PrimitiveTopology::TriangleList;
			config.polygonMode = PolygonMode::Fill;
			config.cullMode = CullMode::Back;
			config.frontFace = FrontFace::CounterClockwise;
			config.depthTestEnable = false;
			config.depthWriteEnable = false;
			config.pushConstantSize = sizeof(PushConstantData);
			config.pushConstantStages = ShaderStage::VertexFragment;
			config.useUniformBuffer = true;

			if (!m_PipelineAdapter->CreatePipeline(PipelineType::Triangle, config))
			{
				LOG_ERROR("Failed to create triangle pipeline");
				return false;
			}
			LOG_INFO("Triangle pipeline created successfully");
		}

		// Create mesh pipeline using shader objects (if shaders loaded)
		{
			VulkanShader* vertShader = m_Resources->GetShader("mesh_vert");
			VulkanShader* fragShader = m_Resources->GetShader("mesh_frag");

			if (vertShader && fragShader)
			{
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
				config.depthCompareOp = CompareOp::GreaterOrEqual;  // Reverse-Z: greater values are closer
				config.pushConstantSize = sizeof(PushConstantData);
				config.pushConstantStages = ShaderStage::VertexFragment;
				config.useUniformBuffer = true;
				config.useTextures = true;
				config.useLighting = true;
				config.useShadowMap = true;

				if (m_PipelineAdapter->CreatePipeline(PipelineType::Mesh, config))
				{
					LOG_INFO("Mesh pipeline created successfully");
				}
				else
				{
					LOG_WARN("Failed to create mesh pipeline");
				}
			}
		}

		{
			PipelineConfig transparentConfig;
			transparentConfig.vertexShaderPath = "Mesh.vert";
			transparentConfig.fragmentShaderPath = "Mesh.frag";  // Same shader for now
			transparentConfig.useVertexInput = true;
			transparentConfig.topology = PrimitiveTopology::TriangleList;
			transparentConfig.polygonMode = PolygonMode::Fill;
			transparentConfig.cullMode = CullMode::Back;
			transparentConfig.frontFace = FrontFace::CounterClockwise;

			// KEY DIFFERENCES for transparency:
			transparentConfig.depthTestEnable = true;   // Still test against depth
			transparentConfig.depthWriteEnable = false; // DON'T write to depth buffer
			transparentConfig.depthCompareOp = CompareOp::GreaterOrEqual;  // Reverse-Z

			transparentConfig.blendEnable = true;  // ENABLE BLENDING
			transparentConfig.srcColorBlendFactor = BlendFactor::SrcAlpha;
			transparentConfig.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;

			transparentConfig.useUniformBuffer = true;
			transparentConfig.useTextures = true;
			transparentConfig.useLighting = true;
			transparentConfig.pushConstantSize = sizeof(PushConstantData);
			transparentConfig.pushConstantStages = ShaderStage::VertexFragment;
			transparentConfig.useShadowMap = true;  // ADD THIS

			if (!m_PipelineAdapter->CreatePipeline(PipelineType::Transparent, transparentConfig))
			{
				LOG_ERROR("Failed to create Transparent pipeline");
			}
		}

		LOG_INFO("=== Pipeline Manager Initialized Successfully ===");
		return true;
	}

	bool Renderer::InitializeShadowMapping()
	{
		LOG_INFO("=== Initializing Shadow Mapping ===");

		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());

		// Create shadow map manager
		m_ShadowManager = std::make_unique<ShadowMapManager>();

		ShadowMapConfig shadowMapConfig;  // Renamed from shadowConfig to avoid shadowing
		shadowMapConfig.resolution = 2048;
		shadowMapConfig.depthFormat = VK_FORMAT_D32_SFLOAT;
		shadowMapConfig.depthBiasConstant = 1.25f;
		shadowMapConfig.depthBiasSlope = 1.75f;
		shadowMapConfig.enablePCF = true;

		if (!m_ShadowManager->Initialize(vkDevice, m_MemoryManager.get(),
			m_DescriptorManager.get(), shadowMapConfig))
		{
			LOG_ERROR("Failed to initialize shadow map manager");
			return false;
		}

		for (uint32_t i = 0; i < FrameSyncManager::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			m_DescriptorManager->UpdateShadowSet(
				i,
				m_ShadowManager->GetShadowMapView(),
				m_ShadowManager->GetShadowSampler()
			);
		}
		LOG_INFO("Updated descriptor manager shadow sets with shadow map");


		// Set shadow render pass for pipeline adapter
		m_PipelineAdapter->SetShadowRenderPass(m_ShadowManager->GetShadowRenderPass());

		// Create shadow pipeline
		{
			PipelineConfig shadowPipelineConfig;  // Renamed to avoid shadowing outer variable
			shadowPipelineConfig.vertexShaderPath = "Shadow.vert";
			shadowPipelineConfig.fragmentShaderPath = "Shadow.frag";
			shadowPipelineConfig.useVertexInput = true;
			shadowPipelineConfig.topology = PrimitiveTopology::TriangleList;
			shadowPipelineConfig.polygonMode = PolygonMode::Fill;

			// Use front-face culling to reduce shadow acne (Peter Panning)
			shadowPipelineConfig.cullMode = CullMode::Back;
			shadowPipelineConfig.frontFace = FrontFace::CounterClockwise;

			// Standard depth test (not reverse-Z for shadow maps)
			shadowPipelineConfig.depthTestEnable = true;
			shadowPipelineConfig.depthWriteEnable = true;
			shadowPipelineConfig.depthCompareOp = CompareOp::LessOrEqual;

			// Enable depth bias to reduce shadow acne
			shadowPipelineConfig.depthBiasEnable = true;
			shadowPipelineConfig.depthBiasConstant = m_ShadowManager->GetDepthBiasConstant();
			shadowPipelineConfig.depthBiasSlope = m_ShadowManager->GetDepthBiasSlope();

			// Shadow pass uses only uniform buffer (for view/proj)
			shadowPipelineConfig.useUniformBuffer = true;
			shadowPipelineConfig.useTextures = false;
			shadowPipelineConfig.useLighting = false;
			shadowPipelineConfig.useShadowMap = false;

			// No color attachment for shadow pass
			shadowPipelineConfig.hasColorAttachment = false;

			shadowPipelineConfig.pushConstantSize = sizeof(PushConstantData);
			shadowPipelineConfig.pushConstantStages = ShaderStage::Vertex;

			if (!m_PipelineAdapter->CreatePipeline(PipelineType::Shadow, shadowPipelineConfig))
			{
				LOG_ERROR("Failed to create shadow pipeline");
				return false;
			}
		}

		LOG_INFO("Shadow mapping initialized successfully");
		return true;
	}

	void Renderer::RecordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex)
	{
		// Reset and begin command buffer
		m_Commands->ResetCommandBuffer(frameIndex);
		m_Commands->BeginCommandBuffer(frameIndex);

		if (m_ShadowEnabled && m_ShadowManager)
		{
			RecordShadowPass(frameIndex);
		}

		// Build clear values array
		// Index 0: Color attachment - clear to background color
		// Index 1: Depth attachment - clear to 0.0 for reverse-Z (near=1.0, far=0.0)
		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = { {m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a} };
		clearValues[1].depthStencil = { 0.0f, 0 };  // depth = 0.0 (far plane in reverse-Z), stencil = 0

		m_Commands->BeginRenderPass(frameIndex,
			m_RenderPasses->GetMainRenderPass(),
			m_RenderPasses->GetFramebuffer(imageIndex),
			m_Swapchain->GetExtent(),
			clearValues.data(),
			static_cast<uint32_t>(clearValues.size()));

		// Execute draw list
		if (!m_FrameDrawList.GetCommands().empty())
		{
			m_Commands->ExecuteDrawList(frameIndex, m_FrameDrawList,
				m_PipelineAdapter.get(),
				m_ViewMatrix, m_ProjectionMatrix);
		}

		// Render UI on top
		if (m_UI)
		{
			m_UI->Render(m_Commands->GetCommandBuffer(frameIndex));
		}

		// End render pass and command buffer
		m_Commands->EndRenderPass(frameIndex);
		m_Commands->EndCommandBuffer(frameIndex);
	}

	// =====================================================================
	// FIX: RecordShadowPass no longer touches m_FrameUniforms.
	//
	// The old version wrote light matrices into the camera UBO, recorded
	// GPU commands, then restored camera data — all CPU-side. But the GPU
	// doesn't execute until submit, so by that time the buffer always
	// contained camera data for BOTH passes.
	//
	// Now the shadow pass has its own dedicated UBO (m_ShadowUniforms),
	// uploaded once in BeginFrame, and binds its own descriptor set here.
	// =====================================================================
	void Renderer::RecordShadowPass(uint32_t frameIndex)
	{
		if (!m_ShadowEnabled || !m_ShadowManager)
		{
			return;
		}

		VkCommandBuffer cmd = m_Commands->GetCommandBuffer(frameIndex);

		// Begin shadow render pass
		VkClearValue depthClear{};
		depthClear.depthStencil = { 1.0f, 0 };  // Standard depth (not reverse-Z)

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = m_ShadowManager->GetShadowRenderPass();
		renderPassInfo.framebuffer = m_ShadowManager->GetShadowFramebuffer();
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = m_ShadowManager->GetShadowExtent();
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &depthClear;

		vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Set viewport and scissor to shadow map size
		VkExtent2D shadowExtent = m_ShadowManager->GetShadowExtent();

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(shadowExtent.width);
		viewport.height = static_cast<float>(shadowExtent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = shadowExtent;
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		// Bind shadow pipeline
		m_PipelineAdapter->BindPipeline(cmd, PipelineType::Shadow);

		VkPipelineLayout shadowLayout = m_PipelineAdapter->GetVulkanManager()->GetPipelineLayout(PipelineType::Shadow);

		// Bind the SHADOW uniform descriptor set (light's view/proj), NOT the camera one
		VkDescriptorSet shadowUniformSet = m_DescriptorManager->GetShadowUniformDescriptorSet(frameIndex);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowLayout,
			0, 1, &shadowUniformSet, 0, nullptr);

		// Render all shadow-casting geometry from the draw list
		for (const auto& drawCmd : m_FrameDrawList.GetCommands())
		{
			// Skip transparent objects and non-mesh pipelines
			if (drawCmd.pipeline == PipelineType::Transparent)
			{
				continue;
			}

			// Skip if no vertex buffer
			if (!drawCmd.vertexBuffer)
			{
				continue;
			}

			// Set push constants (model matrix)
			if (drawCmd.hasPushConstants)
			{
				vkCmdPushConstants(cmd, shadowLayout, VK_SHADER_STAGE_VERTEX_BIT,
					0, sizeof(PushConstantData), &drawCmd.pushConstants);
			}

			// Bind vertex buffer
			VulkanBuffer* vkBuffer = static_cast<VulkanBuffer*>(drawCmd.vertexBuffer);
			VkBuffer vertexBuffers[] = { vkBuffer->GetBuffer() };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

			// Draw
			if (drawCmd.indexBuffer && drawCmd.indexCount > 0)
			{
				VulkanBuffer* vkIndexBuffer = static_cast<VulkanBuffer*>(drawCmd.indexBuffer);
				vkCmdBindIndexBuffer(cmd, vkIndexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(cmd, drawCmd.indexCount, drawCmd.instanceCount, 0, 0, drawCmd.firstInstance);
			}
			else if (drawCmd.vertexCount > 0)
			{
				vkCmdDraw(cmd, drawCmd.vertexCount, drawCmd.instanceCount, 0, drawCmd.firstInstance);
			}
		}

		vkCmdEndRenderPass(cmd);
		// No UBO restore needed — each pass has its own dedicated buffer
	}

	bool Renderer::HandleSwapchainResize()
	{
		LOG_INFO("Handling swapchain resize");

		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());

		// Wait for device to be idle
		m_Device->WaitForIdle();

		// Get ACTUAL current size from the swapchain/surface, not stored values
		VkSurfaceCapabilitiesKHR caps;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
			vkDevice->GetPhysicalDevice(),
			m_Swapchain->GetSurface(),
			&caps
		);

		// If minimized (0x0), skip recreation
		if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0)
		{
			LOG_INFO("Window minimized, skipping swapchain recreation");
			return false;
		}

		// Use the ACTUAL extent from surface capabilities
		uint32_t newWidth = caps.currentExtent.width;
		uint32_t newHeight = caps.currentExtent.height;

		LOG_INFO("Recreating swapchain with width: {}, height: {}", newWidth, newHeight);

		// Update stored dimensions
		m_Width = newWidth;
		m_Height = newHeight;

		// Recreate swapchain
		if (!m_Swapchain->RecreateSwapchain(newWidth, newHeight))
		{
			LOG_ERROR("Failed to recreate swapchain");
			return false;
		}

		// Recreate framebuffers
		if (!m_RenderPasses->RecreateFramebuffers(vkDevice->GetDevice(), m_Swapchain.get()))
		{
			LOG_ERROR("Failed to recreate framebuffers");
			return false;
		}

		// TODO: Update pipeline viewport/scissor if needed

		LOG_INFO("Swapchain resize handled successfully");
		return true;
	}

	void Renderer::UpdateShadowMatrices()
	{
		if (!m_ShadowEnabled || m_CurrentLightingData.numLights == 0)
		{
			m_CurrentLightingData.shadowData.shadowParams.w = 0.0f;
			return;
		}

		const LightData& primaryLight = m_CurrentLightingData.lights[0];

		if (primaryLight.position.w > 0.5f)
		{
			m_CurrentLightingData.shadowData.shadowParams.w = 0.0f;
			return;
		}

		float orthoSize = 25.0f;
		float nearPlane = 0.1f;
		float farPlane = 100.0f;
		float bias = 0.001f;
		float normalBias = 0.02f;

		// Get light direction from the light data
		// primaryLight.position.xyz = direction light is SHINING (e.g., (0,-1,0) = down)
		glm::vec3 lightShineDir = glm::normalize(glm::vec3(primaryLight.position));

		// Position camera along the direction vector
		glm::vec3 lightPos = m_ShadowCenter - lightShineDir * (farPlane * 0.5f);

		// Up vector (handle edge case of looking straight up/down)
		glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
		if (std::abs(glm::dot(lightShineDir, up)) > 0.99f)
		{
			up = glm::vec3(0.0f, 0.0f, 1.0f);
		}

		glm::mat4 lightView = glm::lookAt(lightPos, m_ShadowCenter, up);

		glm::mat4 lightProjection = glm::ortho(
			-orthoSize, orthoSize,
			-orthoSize, orthoSize,
			nearPlane, farPlane
		);

		// Standard Vulkan Y-flip
		lightProjection[1][1] *= -1.0f;

		lightProjection[2][2] *= 0.5f;
		lightProjection[3][2] = lightProjection[3][2] * 0.5f + 0.5f;

		glm::mat4 lightSpaceMatrix = lightProjection * lightView;

		// Fragment shader data
		m_CurrentLightingData.shadowData.lightSpaceMatrix = lightSpaceMatrix;
		m_CurrentLightingData.shadowData.shadowParams = glm::vec4(
			bias, normalBias, 0.0f, 1.0f
		);

		// Shadow pass data
		m_ShadowFrameData.view = lightView;
		m_ShadowFrameData.proj = lightProjection;
		m_ShadowFrameData.time = glm::vec4(m_TotalTime, 0.0f, 0.0f, 0.0f);
		m_ShadowFrameData.cameraPos = glm::vec4(lightPos, 1.0f);

		// DEBUG: Log the light position to verify it's on the correct side
		//LOG_INFO("Shadow camera pos: ({:.2f}, {:.2f}, {:.2f})", lightPos.x, lightPos.y, lightPos.z);
		//LOG_INFO("Light shine dir: ({:.2f}, {:.2f}, {:.2f})", lightShineDir.x, lightShineDir.y, lightShineDir.z);
	}

} // namespace Nightbloom