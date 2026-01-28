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

		// Update per-frame uniform buffer
		uint32_t frameIndex = m_FrameSync->GetCurrentFrame();
		m_CurrentFrameData.view = m_ViewMatrix;
		m_CurrentFrameData.proj = m_ProjectionMatrix;
		m_CurrentFrameData.time.x = m_TotalTime;  // You'll need to track time
		m_CurrentFrameData.cameraPos = glm::vec4(0.0f);  // Set camera position when you have it

		void* mapped = m_FrameUniforms[frameIndex]->GetPersistentMappedPtr();
		if (mapped)
		{
			memcpy(mapped, &m_CurrentFrameData, sizeof(FrameUniformData));
			m_FrameUniforms[frameIndex]->Flush();
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
		// testShader will clean up automatically when it goes out of scope
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



		// Create test geometry
		if (!m_Resources->CreateTestCube())
		{
			LOG_ERROR("Failed to create test geometry");
			return false;
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
				config.depthCompareOp = CompareOp::Less;
				config.pushConstantSize = sizeof(PushConstantData);
				config.pushConstantStages = ShaderStage::VertexFragment;
				config.useUniformBuffer = true;
				config.useTextures = true;

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

		LOG_INFO("=== Pipeline Manager Initialized Successfully ===");
		return true;
	}

	void Renderer::RecordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex)
	{
		// Reset and begin command buffer
		m_Commands->ResetCommandBuffer(frameIndex);
		m_Commands->BeginCommandBuffer(frameIndex);

		// Build clear values array
		// Index 0: Color attachment - clear to background color
		// Index 1: Depth attachment - clear to 1.0 (far plane)
		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = { {m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a} };
		clearValues[1].depthStencil = { 1.0f, 0 };  // depth = 1.0, stencil = 0

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

	bool Renderer::HandleSwapchainResize()
	{
		LOG_INFO("Handling swapchain resize");

		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());

		// Wait for device to be idle
		m_Device->WaitForIdle();

		// Recreate swapchain
		if (!m_Swapchain->RecreateSwapchain(m_Width, m_Height))
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

} // namespace Nightbloom