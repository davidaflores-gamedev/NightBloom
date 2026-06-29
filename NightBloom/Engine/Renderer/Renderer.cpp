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
#include "Engine/Renderer/Vulkan/VulkanTexture.hpp"

//Components
#include "Engine/Renderer/Components/FrameSyncManager.hpp"
#include "Engine/Renderer/Components/RenderPassManager.hpp"
#include "Engine/Renderer/Components/CommandRecorder.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Components/ComputeDispatcher.hpp"
#include "Engine/Renderer/NoiseTextureGenerator.hpp"
#include "Engine/VFX/FireflySystem.hpp"
#include "Engine/VFX/CloudSystem.hpp"
#include "Engine/Hydro/WaterSystem.hpp"
#include "Engine/Renderer/Components/ShadowMapManager.hpp"
#include "Engine/Renderer/Components/GpuProfiler.hpp"
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

		// Initialize compute support (optional - continues without if it fails)
		if (!InitializeCompute())
		{
			LOG_WARN("Failed to initialize compute - continuing without compute support");
			m_ComputeEnabled = false;
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

		CleanupCompute();

		if (m_GpuProfiler)
		{
			m_GpuProfiler->Cleanup();
			m_GpuProfiler.reset();
		}

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

		// Resources (textures) must be cleaned up while the descriptor manager
		// is still alive — VulkanTexture::Cleanup() frees its own descriptor
		// set via the manager it was allocated from. Tearing down the manager
		// first leaves that pointer dangling.
		if (m_Resources)
		{
			m_Resources->Cleanup();
			m_Resources.reset();
		}

		if (m_DescriptorManager)
		{
			m_DescriptorManager->Cleanup();
			m_DescriptorManager.reset();
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
		float newTotalTime = std::chrono::duration<float>(currentTime - startTime).count();
		m_LastDeltaTime = newTotalTime - m_TotalTime;
		m_TotalTime = newTotalTime;

		uint32_t frameIndex = m_FrameSync->GetCurrentFrame();

		if (m_CloudSystem)
		{
			m_CloudSystem->UpdateParams(frameIndex, m_LastDeltaTime);
		}

		// =====================================================================
		// FIX: Compute shadow matrices FIRST so m_ShadowFrameData is populated
		// before we upload it to the GPU buffer below.
		// Previously this was called AFTER the upload, meaning the shadow UBO
		// always contained stale data from the previous frame (or zeros).
		// =====================================================================
		UpdateShadowMatrices();

		// Water level fed to shaders via the reserved time.yz slots: time.y =
		// water surface Y, time.z = water-enabled flag. Grass.vert reads these to
		// cull blades that would otherwise grow underwater (the grass system is
		// independent of water, so it can't know the waterline on its own).
		const float waterLevel = m_WaterSystem ? m_WaterSystem->GetWaterY() : 0.0f;
		const float waterEnabled = m_WaterSystem ? 1.0f : 0.0f;

		// Update camera uniform buffer (set 0 in main pass)
		m_CurrentFrameData.view = m_ViewMatrix;
		m_CurrentFrameData.proj = m_ProjectionMatrix;
		m_CurrentFrameData.time.x = m_TotalTime;
		m_CurrentFrameData.time.y = waterLevel;
		m_CurrentFrameData.time.z = waterEnabled;
		m_CurrentFrameData.cameraPos = glm::vec4(m_CameraPosition, 1.0f);
		m_CurrentFrameData.invView = glm::inverse(m_ViewMatrix);
		m_CurrentFrameData.invProj = glm::inverse(m_ProjectionMatrix);

		void* mapped = m_FrameUniforms[frameIndex]->GetPersistentMappedPtr();
		if (mapped)
		{
			memcpy(mapped, &m_CurrentFrameData, sizeof(FrameUniformData));
			m_FrameUniforms[frameIndex]->Flush();
		}

		// Upload shadow uniform buffers (set 0 in shadow pass - each cascade's light view/proj)
		// Now happens AFTER UpdateShadowMatrices has populated m_ShadowFrameData
		for (uint32_t c = 0; c < NUM_CASCADES; ++c)
		{
			void* shadowMapped = m_ShadowUniforms[frameIndex][c]->GetPersistentMappedPtr();
			if (shadowMapped)
			{
				memcpy(shadowMapped, &m_ShadowFrameData[c], sizeof(FrameUniformData));
				m_ShadowUniforms[frameIndex][c]->Flush();
			}
		}

		// Upload lighting UBO (set 2)
		void* lightMapped = m_LightingUniforms[frameIndex]->GetPersistentMappedPtr();
		if (lightMapped)
		{
			memcpy(lightMapped, &m_CurrentLightingData, sizeof(SceneLightingData));
			m_LightingUniforms[frameIndex]->Flush();
		}

		// Upload reflection UBO (set 0 in the reflection pass). The camera is
		// mirrored across the water plane (y = waterY); the projection is left
		// as-is (no oblique clip plane in v1 — below-water geometry that pierces
		// the surface can leak into the reflection; logged as a follow-up).
		if (m_WaterSystem)
		{
			const float waterY = m_WaterSystem->GetWaterY();

			// Reflect world-space positions across plane y = waterY:
			//   (x, y, z) -> (x, 2*waterY - y, z)
			glm::mat4 mirror(1.0f);
			mirror[1][1] = -1.0f;          // negate Y
			mirror[3][1] = 2.0f * waterY;  // translate Y by 2*waterY

			m_ReflectionFrameData.view = m_ViewMatrix * mirror;
			m_ReflectionFrameData.proj = m_ProjectionMatrix;
			m_ReflectionFrameData.time.x = m_TotalTime;
			// Cull reflected grass at the waterline too (otherwise underwater
			// blades, mirrored, would appear floating above the surface).
			m_ReflectionFrameData.time.y = waterY;
			m_ReflectionFrameData.time.z = 1.0f;

			glm::vec3 mirroredCam = m_CameraPosition;
			mirroredCam.y = 2.0f * waterY - mirroredCam.y;
			m_ReflectionFrameData.cameraPos = glm::vec4(mirroredCam, 1.0f);
			m_ReflectionFrameData.invView = glm::inverse(m_ReflectionFrameData.view);
			m_ReflectionFrameData.invProj = glm::inverse(m_ReflectionFrameData.proj);

			void* reflMapped = m_ReflectionUniforms[frameIndex]->GetPersistentMappedPtr();
			if (reflMapped)
			{
				memcpy(reflMapped, &m_ReflectionFrameData, sizeof(FrameUniformData));
				m_ReflectionUniforms[frameIndex]->Flush();
			}
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

	void Renderer::RunComputeTest()
	{
	}

	void Renderer::PrintComputeTestResults()
	{
		if (!m_ComputeEnabled || !m_ComputeTestOutputBuffer)
		{
			LOG_WARN("Compute test not available");
			return;
		}

		// Wait for GPU to finish
		m_Device->WaitForIdle();

		// Capture current time for verification (matches what was used in last dispatch)
		float capturedTime = m_TotalTime;
		float timeOffset = std::sin(capturedTime);

		size_t bufferSize = COMPUTE_TEST_ELEMENT_COUNT * sizeof(float);

		// Create a host-visible readback buffer
		VulkanBuffer* readbackBuffer = m_Resources->CreateStorageBuffer(
			"ComputeTestReadback",
			bufferSize,
			true  // Host visible for CPU read
		);

		if (!readbackBuffer)
		{
			LOG_ERROR("Failed to create readback buffer");
			return;
		}

		// Copy GPU buffer to readback buffer
		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());
		VulkanSingleTimeCommand cmd(vkDevice, m_Resources->GetTransferCommandPool());

		VkCommandBuffer cmdBuffer = cmd.Begin();

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = 0;
		copyRegion.size = bufferSize;

		vkCmdCopyBuffer(cmdBuffer,
			m_ComputeTestOutputBuffer->GetBuffer(),
			readbackBuffer->GetBuffer(),
			1, &copyRegion);

		cmd.End();

		// Read results
		void* mapped = readbackBuffer->Map();
		if (mapped)
		{
			float* results = static_cast<float*>(mapped);

			LOG_INFO("=== Compute Test Results ===");
			LOG_INFO("Time: {:.3f}s, sin(time) offset: {:.3f}", capturedTime, timeOffset);
			LOG_INFO("Formula: output[i] = input[i] * 2.0 + sin(time)");
			LOG_INFO("");
			LOG_INFO("First 8 values:");

			bool success = true;
			for (uint32_t i = 0; i < 8 && i < COMPUTE_TEST_ELEMENT_COUNT; ++i)
			{
				float input = static_cast<float>(i + 1);
				float expected = input * 2.0f + timeOffset;
				float actual = results[i];
				float error = std::abs(actual - expected);

				// Allow small floating point tolerance
				bool match = error < 0.01f;

				if (match)
				{
					LOG_INFO("  [{}]: {:.3f} (expected: {:.3f}) OK", i, actual, expected);
				}
				else
				{
					LOG_ERROR("  [{}]: {:.3f} (expected: {:.3f}) MISMATCH", i, actual, expected);
					success = false;
				}
			}

			LOG_INFO("");
			if (success)
			{
				LOG_INFO("Compute test PASSED!");
			}
			else
			{
				LOG_ERROR("Compute test FAILED!");
			}

			readbackBuffer->Unmap();
		}

		// Clean up readback buffer
		m_Resources->DestroyBuffer("ComputeTestReadback");
	}

	bool Renderer::RegenerateNoisePreview(const NoiseTextureDesc& desc)
	{
		if (!m_NoiseGenerator) return false;

		m_Device->WaitForIdle();

		if (m_NoisePreview)
		{
			delete m_NoisePreview;
			m_NoisePreview = nullptr;
		}

		// Force depth=1 so it gets a 2D image view (ImGui-displayable)
		NoiseTextureDesc previewDesc = desc;
		previewDesc.depth = 1;

		m_NoisePreview = m_NoiseGenerator->Generate(previewDesc, m_ComputeDispatcher.get());
		return m_NoisePreview != nullptr;
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

		
		if (!m_Resources->LoadShader("terrain_vert", ShaderStage::Vertex, "Terrain.vert"))
		{
			LOG_WARN("Failed to load terrain vertex shader - continuing without terrain pipeline");
		}
			
		if (!m_Resources->LoadShader("terrain_frag", ShaderStage::Fragment, "Terrain.frag"))
		{
			LOG_WARN("Failed to load terrain fragment shader - continuing without terrain pipeline");
		}

		if (!m_Resources->LoadShader("grass_vert", ShaderStage::Vertex, "Grass.vert"))
		{
			LOG_WARN("Failed to load grass vertex shader - continuing without foliage pipeline");
		}

		if (!m_Resources->LoadShader("grass_frag", ShaderStage::Fragment, "Grass.frag"))
		{
			LOG_WARN("Failed to load grass fragment shader - continuing without foliage pipeline");
		}

		if (!m_Resources->LoadShader("firefly_vert", ShaderStage::Vertex, "Firefly.vert"))
		{
			LOG_WARN("Failed to load firefly vertex shader - continuing without firefly pipeline");
		}

		if (!m_Resources->LoadShader("firefly_frag", ShaderStage::Fragment, "Firefly.frag"))
		{
			LOG_WARN("Failed to load firefly fragment shader - continuing without firefly pipeline");
		}

		if (!m_Resources->LoadShader("clouds_vert", ShaderStage::Vertex, "Clouds.vert"))
		{
			LOG_WARN("Failed to load clouds vertex shader - continuing without clouds pipeline");
		}

		if (!m_Resources->LoadShader("clouds_frag", ShaderStage::Fragment, "Clouds.frag"))
		{
			LOG_WARN("Failed to load clouds fragment shader - continuing without clouds pipeline");
		}

		if (!m_Resources->LoadShader("postprocess_vert", ShaderStage::Vertex, "PostProcess.vert"))
		{
			LOG_ERROR("Failed to load post-process vertex shader");
			return false;
		}

		if (!m_Resources->LoadShader("postprocess_frag", ShaderStage::Fragment, "PostProcess.frag"))
		{
			LOG_ERROR("Failed to load post-process fragment shader");
			return false;
		}

		if (!m_Resources->LoadShader("water_vert", ShaderStage::Vertex, "Water.vert"))
		{
			LOG_WARN("Failed to load water vertex shader - continuing without water pipeline");
		}

		if (!m_Resources->LoadShader("water_frag", ShaderStage::Fragment, "Water.frag"))
		{
			LOG_WARN("Failed to load water fragment shader - continuing without water pipeline");
		}

		LOG_INFO("Shaders loaded successfully");
		return true;
	}

	VkDevice Renderer::GetVkDevice() const
	{
		return static_cast<VulkanDevice*>(m_Device.get())->GetDevice();
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

		// Initialize render passes — request 2x MSAA on the offscreen scene
		// pass, clamped to what the device's color+depth attachments support.
		// 2x is a deliberate cost/quality balance: on these clean edges it's
		// nearly indistinguishable from 4x but roughly halves the MSAA
		// bandwidth/resolve cost. Bump the cap to VK_SAMPLE_COUNT_4_BIT here if
		// you want maximum edge quality and can spare the fill cost.
		VkSampleCountFlagBits sceneSamples = vkDevice->GetMaxUsableSampleCount(VK_SAMPLE_COUNT_2_BIT);
		m_RenderPasses = std::make_unique<RenderPassManager>();
		if (!m_RenderPasses->Initialize(vkDevice->GetDevice(), m_Swapchain.get(), m_MemoryManager.get(), sceneSamples))
		{
			LOG_ERROR("Failed to initialize render passes");
			return false;
		}
		LOG_INFO("Scene pass MSAA: {}x", static_cast<int>(sceneSamples));

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
			for (uint32_t c = 0; c < NUM_CASCADES; ++c)
			{
				std::string bufferName = "ShadowUniform_f" + std::to_string(i) + "_c" + std::to_string(c);
				m_ShadowUniforms[i][c] = m_Resources->CreateUniformBuffer(
					bufferName, sizeof(FrameUniformData));

				if (!m_ShadowUniforms[i][c])
				{
					LOG_ERROR("Failed to create shadow uniform buffer for frame {} cascade {}", i, c);
					return false;
				}

				// Point the shadow uniform descriptor set at this buffer
				m_DescriptorManager->UpdateShadowUniformSet(i, c,
					m_ShadowUniforms[i][c]->GetBuffer(),
					sizeof(FrameUniformData));
			}
		}
		LOG_INFO("Shadow uniform buffers created");

		// =================================================================
		// Reflection uniform buffers (set 0 in the planar-reflection pass —
		// the mirror-flipped camera's view/proj). Same pattern as shadow.
		// =================================================================
		LOG_INFO("Creating reflection uniform buffers");
		for (uint32_t i = 0; i < 2; ++i)
		{
			std::string bufferName = "ReflectionUniform_" + std::to_string(i);
			m_ReflectionUniforms[i] = m_Resources->CreateUniformBuffer(
				bufferName, sizeof(FrameUniformData));

			if (!m_ReflectionUniforms[i])
			{
				LOG_ERROR("Failed to create reflection uniform buffer for frame {}", i);
				return false;
			}

			m_DescriptorManager->UpdateReflectionUniformSet(i,
				m_ReflectionUniforms[i]->GetBuffer(),
				sizeof(FrameUniformData));
		}
		LOG_INFO("Reflection uniform buffers created");

		// Create test geometry
		if (!m_Resources->CreateTestCube())
		{
			LOG_ERROR("Failed to create test geometry");
			return false;
		}

		if (!m_Resources->CreateGroundPlane(200.0f, 10.0f))
		{
			LOG_WARN("Failed to create ground plane");
			// Non-fatal � continue without it
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

		// GPU profiler (timestamp queries). Non-fatal if unsupported.
		m_GpuProfiler = std::make_unique<GpuProfiler>();
		m_GpuProfiler->Initialize(vkDevice);

		// Initialize UI (optional) — targets the post-process render pass,
		// not the scene one: UI now draws after the AA composite, directly
		// onto the swapchain target, so panel text doesn't get blurred by
		// the filter. ImGui's Vulkan backend builds its own pipeline against
		// whatever render pass it's given here, so this must match wherever
		// m_UI->Render() actually gets called (see RecordPostProcessPass).
		m_UI = std::make_unique<UIManager>();
		if (!m_UI->Initialize(vkDevice, m_WindowHandle,
			m_RenderPasses->GetPostProcessRenderPass(),
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
			m_RenderPasses->GetSceneRenderPass(),
			m_Swapchain->GetExtent(),
			m_DescriptorManager.get()))
		{
			LOG_ERROR("Failed to initialize pipeline adapter");
			return false;
		}

		// All scene-pass pipelines must be created with the scene MSAA sample
		// count (the shadow/post-process passes stay single-sample — handled
		// per-type inside the adapter). Must be set before any pipeline is created.
		m_PipelineAdapter->SetSampleCount(m_RenderPasses->GetSampleCount());

		// LOAD SHADERS FIRST!
		if (!LoadShaders())
		{
			LOG_ERROR("Failed to load shaders");
			return false;
		}

		// ---- Triangle pipeline -------------------------------------------------------
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

		// ---- Mesh pipeline -------------------------------------------------------
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

		// ---- Transparent pipeline -------------------------------------------------------
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

		// ---- Terrain pipeline -------------------------------------------------------
		{
			VulkanShader* terrainVert = m_Resources->GetShader("terrain_vert");
			VulkanShader* terrainFrag = m_Resources->GetShader("terrain_frag");

			if (terrainVert && terrainFrag)
			{
				PipelineConfig terrainConfig;
				terrainConfig.vertexShader = terrainVert;
				terrainConfig.fragmentShader = terrainFrag;
				terrainConfig.useVertexInput = true;
				terrainConfig.topology = PrimitiveTopology::TriangleList;
				terrainConfig.polygonMode = PolygonMode::Fill;
				terrainConfig.cullMode = CullMode::Back;
				terrainConfig.frontFace = FrontFace::CounterClockwise;

				// Reverse-Z depth (matches the rest of the scene)
				terrainConfig.depthTestEnable = true;
				terrainConfig.depthWriteEnable = true;
				terrainConfig.depthCompareOp = CompareOp::GreaterOrEqual;

				// Push constants carry model matrix + heightScale + texelSize
				terrainConfig.pushConstantSize = sizeof(PushConstantData);
				terrainConfig.pushConstantStages = ShaderStage::VertexFragment;

				// Descriptor sets: 0=uniform, 1=albedo, 2=lighting, 3=shadow, 4=heightmap
				terrainConfig.useUniformBuffer = true;
				terrainConfig.useTextures = true;
				terrainConfig.useLighting = true;
				terrainConfig.useShadowMap = true;
				terrainConfig.useHeightmap = true;   // <-- new flag (set 4)

				if (m_PipelineAdapter->CreatePipeline(PipelineType::Terrain, terrainConfig))
				{
					LOG_INFO("Terrain pipeline created successfully");
				}
				else
				{
					LOG_WARN("Failed to create terrain pipeline � terrain will not render");
				}
			}
			else
			{
				LOG_WARN("Terrain shaders not found - skipping terrain pipeline");
			}
		}

		// ---- Foliage pipeline -------------------------------------------------------
		{
			VulkanShader* grassVert = m_Resources->GetShader("grass_vert");
			VulkanShader* grassFrag = m_Resources->GetShader("grass_frag");

			if (grassVert && grassFrag)
			{
				PipelineConfig grassConfig;
				grassConfig.vertexShader = grassVert;
				grassConfig.fragmentShader = grassFrag;

				// Real blade mesh (VertexPNT-shaped), instanced via gl_InstanceIndex
				// reading the foliage storage buffer - not procedural like Firefly.
				grassConfig.useVertexInput = true;
				grassConfig.topology = PrimitiveTopology::TriangleList;
				grassConfig.polygonMode = PolygonMode::Fill;
				// Blades have no thickness - CullMode::Back would make them
				// disappear when viewed from behind, same reasoning as Firefly's
				// billboards (though for a different underlying cause).
				grassConfig.cullMode = CullMode::None;
				grassConfig.frontFace = FrontFace::CounterClockwise;

				grassConfig.depthTestEnable = true;
				grassConfig.depthWriteEnable = true;
				grassConfig.depthCompareOp = CompareOp::GreaterOrEqual;

				// customData = (slopeThreshold, heightScale, worldSize, windStrength)
				grassConfig.pushConstantSize = sizeof(PushConstantData);
				grassConfig.pushConstantStages = ShaderStage::VertexFragment;

				// Descriptor sets: 0=uniform, 1=foliage storage (no texture set,
				// so this lands at set 1 same as Firefly), 2=lighting, 3=shadow,
				// 4=heightmap. See VulkanPipelineAdapter::CreatePipeline's
				// if-chain order and Grass.vert/.frag's layout(set=N) decls.
				grassConfig.useUniformBuffer = true;
				grassConfig.useFoliageStorage = true;
				grassConfig.useLighting = true;
				grassConfig.useShadowMap = true;
				grassConfig.useHeightmap = true;

				if (m_PipelineAdapter->CreatePipeline(PipelineType::Foliage, grassConfig))
				{
					LOG_INFO("Foliage pipeline created successfully");
				}
				else
				{
					LOG_WARN("Failed to create foliage pipeline - grass will not render");
				}
			}
			else
			{
				LOG_WARN("Grass shaders not found - skipping foliage pipeline");
			}
		}

		// ---- Clouds pipeline --------------------------------------------------------
		{
			VulkanShader* cloudsVert = m_Resources->GetShader("clouds_vert");
			VulkanShader* cloudsFrag = m_Resources->GetShader("clouds_frag");

			if (cloudsVert && cloudsFrag)
			{
				PipelineConfig cloudsConfig;
				cloudsConfig.vertexShader = cloudsVert;
				cloudsConfig.fragmentShader = cloudsFrag;

				// No vertex/index buffer - Clouds.vert generates a full-screen
				// triangle procedurally from gl_VertexIndex.
				cloudsConfig.useVertexInput = false;
				cloudsConfig.topology = PrimitiveTopology::TriangleList;
				cloudsConfig.polygonMode = PolygonMode::Fill;
				cloudsConfig.cullMode = CullMode::None;
				cloudsConfig.frontFace = FrontFace::CounterClockwise;

				// Depth-tested against the scene (so terrain/mountains occlude
				// clouds via the normal depth test - see Clouds.vert's fixed
				// "at infinity" depth output), doesn't write depth.
				cloudsConfig.depthTestEnable = true;
				cloudsConfig.depthWriteEnable = false;
				cloudsConfig.depthCompareOp = CompareOp::GreaterOrEqual;

				// Standard alpha blend (not additive like Firefly's glow) -
				// clouds composite over the sky/background normally.
				cloudsConfig.blendEnable = true;
				cloudsConfig.srcColorBlendFactor = BlendFactor::SrcAlpha;
				cloudsConfig.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;

				// Descriptor sets: 0=cloud raymarch result (only set needed -
				// the raymarch itself, and the FrameUniforms/lighting data it
				// needs, moved to CloudRaymarch.comp; this pass just samples
				// the low-res result and composites it).
				cloudsConfig.useCloudResult = true;

				if (m_PipelineAdapter->CreatePipeline(PipelineType::Clouds, cloudsConfig))
				{
					LOG_INFO("Clouds pipeline created successfully");
				}
				else
				{
					LOG_WARN("Failed to create clouds pipeline - clouds will not render");
				}
			}
			else
			{
				LOG_WARN("Clouds shaders not found - skipping clouds pipeline");
			}
		}

		// ---- Water pipeline ---------------------------------------------------------
		{
			VulkanShader* waterVert = m_Resources->GetShader("water_vert");
			VulkanShader* waterFrag = m_Resources->GetShader("water_frag");

			if (waterVert && waterFrag)
			{
				PipelineConfig waterConfig;
				waterConfig.vertexShader = waterVert;
				waterConfig.fragmentShader = waterFrag;

				// Flat VertexPNT plane (reuses the terrain grid generator). Same
				// CCW winding as the terrain mesh, so standard back-face cull.
				waterConfig.useVertexInput = true;
				waterConfig.topology = PrimitiveTopology::TriangleList;
				waterConfig.polygonMode = PolygonMode::Fill;
				waterConfig.cullMode = CullMode::Back;
				waterConfig.frontFace = FrontFace::CounterClockwise;

				// Depth-tested against terrain/foliage, but does NOT write depth —
				// water is a translucent surface that composites over the scene
				// (drawn after opaque geometry via the PipelineType sort order).
				waterConfig.depthTestEnable = true;
				waterConfig.depthWriteEnable = false;
				waterConfig.depthCompareOp = CompareOp::GreaterOrEqual;

				waterConfig.blendEnable = true;
				waterConfig.srcColorBlendFactor = BlendFactor::SrcAlpha;
				waterConfig.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;

				// customData = (waveAmplitude, waveSpeed, fresnelPower, alpha).
				waterConfig.pushConstantSize = sizeof(PushConstantData);
				waterConfig.pushConstantStages = ShaderStage::VertexFragment;

				// Descriptor sets: 0=uniform (scene camera), 1=lighting (sun for
				// Fresnel/specular), 2=reflection target. useReflectionInput is
				// pushed last in the adapter's layout chain so, with only these
				// three flags set, it lands at set 2 — see Water.frag's
				// layout(set=N) decls and CommandRecorder's Water binding block.
				waterConfig.useUniformBuffer = true;
				waterConfig.useLighting = true;
				waterConfig.useReflectionInput = true;

				if (m_PipelineAdapter->CreatePipeline(PipelineType::Water, waterConfig))
				{
					LOG_INFO("Water pipeline created successfully");
				}
				else
				{
					LOG_WARN("Failed to create water pipeline - water will not render");
				}
			}
			else
			{
				LOG_WARN("Water shaders not found - skipping water pipeline");
			}
		}

		// ---- Firefly pipeline -------------------------------------------------------
		{
			VulkanShader* fireflyVert = m_Resources->GetShader("firefly_vert");
			VulkanShader* fireflyFrag = m_Resources->GetShader("firefly_frag");

			if (fireflyVert && fireflyFrag)
			{
				PipelineConfig fireflyConfig;
				fireflyConfig.vertexShader = fireflyVert;
				fireflyConfig.fragmentShader = fireflyFrag;

				// No vertex/index buffer - Firefly.vert generates the billboard
				// quad procedurally from gl_VertexIndex.
				fireflyConfig.useVertexInput = false;
				fireflyConfig.topology = PrimitiveTopology::TriangleList;
				fireflyConfig.polygonMode = PolygonMode::Fill;
				fireflyConfig.cullMode = CullMode::None; // billboards always face the camera
				fireflyConfig.frontFace = FrontFace::CounterClockwise;

				// Depth-tested against the scene, but doesn't write depth -
				// overlapping glows shouldn't punch holes in the depth buffer.
				fireflyConfig.depthTestEnable = true;
				fireflyConfig.depthWriteEnable = false;
				fireflyConfig.depthCompareOp = CompareOp::GreaterOrEqual;

				// Additive-alpha glow blend
				fireflyConfig.blendEnable = true;
				fireflyConfig.srcColorBlendFactor = BlendFactor::SrcAlpha;
				fireflyConfig.dstColorBlendFactor = BlendFactor::One;

				// Descriptor sets: 0=uniform (camera), 1=firefly agent storage buffer
				fireflyConfig.useUniformBuffer = true;
				fireflyConfig.useFireflyStorage = true;

				if (m_PipelineAdapter->CreatePipeline(PipelineType::Firefly, fireflyConfig))
				{
					LOG_INFO("Firefly pipeline created successfully");
				}
				else
				{
					LOG_WARN("Failed to create firefly pipeline - fireflies will not render");
				}
			}
			else
			{
				LOG_WARN("Firefly shaders not found - skipping firefly pipeline");
			}
		}

		// ---- PostProcess pipeline (FXAA composite) -----------------------------------
		// Targets a dedicated render pass (writes the actual swapchain image,
		// not the offscreen scene-color texture every other pipeline above
		// targets), so it needs the same SetXRenderPass override Shadow/
		// TerrainShadow already use - set before CreatePipeline, mirroring
		// InitializeShadowMapping()'s SetShadowRenderPass() call.
		{
			VulkanShader* postProcessVert = m_Resources->GetShader("postprocess_vert");
			VulkanShader* postProcessFrag = m_Resources->GetShader("postprocess_frag");

			if (postProcessVert && postProcessFrag)
			{
				m_PipelineAdapter->SetPostProcessRenderPass(m_RenderPasses->GetPostProcessRenderPass());

				PipelineConfig postProcessConfig;
				postProcessConfig.vertexShader = postProcessVert;
				postProcessConfig.fragmentShader = postProcessFrag;

				// No vertex/index buffer - PostProcess.vert generates the
				// full-screen triangle procedurally from gl_VertexIndex.
				postProcessConfig.useVertexInput = false;
				postProcessConfig.topology = PrimitiveTopology::TriangleList;
				postProcessConfig.polygonMode = PolygonMode::Fill;
				postProcessConfig.cullMode = CullMode::None;
				postProcessConfig.frontFace = FrontFace::CounterClockwise;

				// No depth attachment in this pass at all (see
				// RenderPassManager::CreatePostProcessRenderPass) - the
				// full-screen triangle overwrites every pixel unconditionally.
				postProcessConfig.depthTestEnable = false;
				postProcessConfig.depthWriteEnable = false;
				postProcessConfig.blendEnable = false;

				// Descriptor sets: 0=scene-color sampler (only input this pass needs)
				postProcessConfig.usePostProcessInput = true;

				// AA on/off toggle (Debug Panel) — bypasses this entirely
				// independent of CommandRecorder's shared PushConstantData,
				// since this pass is recorded directly (see RecordPostProcessPass),
				// not through the normal per-draw-command path.
				postProcessConfig.pushConstantSize = sizeof(int);
				postProcessConfig.pushConstantStages = ShaderStage::Fragment;

				if (m_PipelineAdapter->CreatePipeline(PipelineType::PostProcess, postProcessConfig))
				{
					LOG_INFO("PostProcess pipeline created successfully");
				}
				else
				{
					LOG_ERROR("Failed to create post-process pipeline - nothing will reach the screen");
					return false;
				}

				// Allocate + point the descriptor set at the scene-color
				// texture RenderPassManager owns. Re-updated in
				// HandleSwapchainResize since that texture gets recreated then.
				m_PostProcessInputSet = m_DescriptorManager->AllocatePostProcessInputSet();
				if (m_PostProcessInputSet == VK_NULL_HANDLE)
				{
					LOG_ERROR("Failed to allocate post-process input descriptor set");
					return false;
				}
				m_DescriptorManager->UpdatePostProcessInputSet(m_PostProcessInputSet,
					m_RenderPasses->GetSceneColorImageView(), m_RenderPasses->GetSceneColorSampler());
			}
			else
			{
				LOG_ERROR("PostProcess shaders not found - nothing will reach the screen");
				return false;
			}
		}

		// ---- Reflection input set (Water set 2: the reflection target sampler).
		// Allocated once and pointed at the reflection target; re-pointed in
		// HandleSwapchainResize since the target is recreated then. Handed to the
		// CommandRecorder, which binds it for every Water draw.
		m_ReflectionInputSet = m_DescriptorManager->AllocateReflectionInputSet();
		if (m_ReflectionInputSet == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to allocate reflection input descriptor set");
			return false;
		}
		m_DescriptorManager->UpdateReflectionInputSet(m_ReflectionInputSet,
			m_RenderPasses->GetReflectionColorImageView(), m_RenderPasses->GetReflectionColorSampler());
		m_Commands->SetReflectionInputSet(m_ReflectionInputSet);

		LOG_INFO("=== Pipeline Manager Initialized Successfully ===");
		return true;
	}

	bool Renderer::InitializeCompute()
	{
		LOG_INFO("=== Initializing Compute Support ===");

		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());

		// Create compute dispatcher
		m_ComputeDispatcher = std::make_unique<ComputeDispatcher>();
		if (!m_ComputeDispatcher->Initialize(vkDevice, m_PipelineAdapter->GetVulkanManager()))
		{
			LOG_ERROR("Failed to initialize compute dispatcher");
			return false;
		}

		// Create compute pipeline - go directly to VulkanPipelineManager
		// since we need to specify custom descriptor set layouts
		{
			VulkanPipelineConfig vkConfig;
			vkConfig.computeShaderPath = "MultiplyByTwo.comp";
			vkConfig.pushConstantSize = sizeof(ComputePushConstants);
			vkConfig.descriptorSetLayouts = { m_DescriptorManager->GetComputeStorageSetLayout() };

			if (!m_PipelineAdapter->GetVulkanManager()->CreatePipeline(PipelineType::Compute, vkConfig))
			{
				LOG_WARN("Failed to create compute pipeline - continuing without compute");
				return false;
			}
			LOG_INFO("Compute pipeline created successfully");
		}

		// Create test buffers using ResourceManager
		LOG_INFO("Creating compute test buffers");

		size_t bufferSize = COMPUTE_TEST_ELEMENT_COUNT * sizeof(float);

		// Input buffer - device local storage buffer
		m_ComputeTestInputBuffer = m_Resources->CreateStorageBuffer(
			"ComputeTestInput",
			bufferSize,
			false  // GPU only
		);

		if (!m_ComputeTestInputBuffer)
		{
			LOG_ERROR("Failed to create compute test input buffer");
			return false;
		}

		// Output buffer - device local storage buffer  
		m_ComputeTestOutputBuffer = m_Resources->CreateStorageBuffer(
			"ComputeTestOutput",
			bufferSize,
			false  // GPU only
		);

		if (!m_ComputeTestOutputBuffer)
		{
			LOG_ERROR("Failed to create compute test output buffer");
			return false;
		}

		// Upload initial test data (1.0, 2.0, 3.0, ...)
		{
			std::vector<float> testData(COMPUTE_TEST_ELEMENT_COUNT);
			for (uint32_t i = 0; i < COMPUTE_TEST_ELEMENT_COUNT; ++i)
			{
				testData[i] = static_cast<float>(i + 1);
			}

			// VulkanBuffer::UploadData handles staging internally
			if (!m_ComputeTestInputBuffer->UploadData(
				testData.data(),
				bufferSize,
				0,
				m_Resources->GetTransferCommandPool()))
			{
				LOG_ERROR("Failed to upload compute test data");
				return false;
			}
			LOG_INFO("Uploaded {} floats to compute input buffer", COMPUTE_TEST_ELEMENT_COUNT);
		}

		// Allocate and update descriptor set
		m_ComputeTestDescriptorSet = m_DescriptorManager->AllocateComputeStorageSet();
		if (m_ComputeTestDescriptorSet == VK_NULL_HANDLE)
		{
			LOG_ERROR("Failed to allocate compute test descriptor set");
			return false;
		}

		m_DescriptorManager->UpdateComputeStorageSet(
			m_ComputeTestDescriptorSet,
			m_ComputeTestInputBuffer->GetBuffer(), bufferSize,
			m_ComputeTestOutputBuffer->GetBuffer(), bufferSize
		);

		// Initialize noise texture generator
		m_NoiseGenerator = std::make_unique<NoiseTextureGenerator>();
		if (!m_NoiseGenerator->Initialize(
			vkDevice,
			m_MemoryManager.get(),
			m_Resources->GetTransferCommandPool(),
			m_DescriptorManager.get()))
		{
			LOG_WARN("Failed to initialize NoiseTextureGenerator - continuing without noise");
		}
		else
		{
			// Sanity check: generate a small test noise texture
			NoiseTextureDesc testDesc;
			testDesc.width = 64;
			testDesc.height = 64;
			testDesc.depth = 64;
			testDesc.noiseType = NoiseType::Perlin;
			testDesc.octaves = 4;
			testDesc.frequency = 4.0f;
			testDesc.debugName = "TestNoise";

			m_TestNoise = m_NoiseGenerator->Generate(testDesc, m_ComputeDispatcher.get());
			if (!m_TestNoise)
			{
				LOG_WARN("Test noise generation failed");
			}
		}

		// Generate initial 2D preview for the editor panel
		NoiseTextureDesc previewDesc;
		previewDesc.width = 256;
		previewDesc.height = 256;
		previewDesc.depth = 1;
		previewDesc.noiseType = NoiseType::Perlin;
		previewDesc.octaves = 4;
		previewDesc.frequency = 4.0f;
		previewDesc.debugName = "NoisePreview";
		m_NoisePreview = m_NoiseGenerator->Generate(previewDesc, m_ComputeDispatcher.get());

		m_ComputeEnabled = true;
		LOG_INFO("Compute support initialized successfully");
		return true;
	}

	bool Renderer::InitializeShadowMapping()
	{
		LOG_INFO("=== Initializing Shadow Mapping ===");

		VulkanDevice* vkDevice = static_cast<VulkanDevice*>(m_Device.get());

		// Create shadow map manager
		m_ShadowManager = std::make_unique<ShadowMapManager>();

		ShadowMapConfig shadowMapConfig;  // Renamed from shadowConfig to avoid shadowing
		shadowMapConfig.resolution = 4096;
		shadowMapConfig.depthFormat = VK_FORMAT_D32_SFLOAT;
		shadowMapConfig.depthBiasConstant = 1.0f;
		shadowMapConfig.depthBiasSlope = 1.5f;
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

			// MUST STAY CullMode::Back. Front-face culling is the textbook acne fix
			// (store back-face depth so lit front faces never self-shadow), but in THIS
			// engine it caused a drastic FPS drop (confirmed 2026-06-26, ~1500fps -> tanked)
			// and was reverted. Acne here is a BIAS-TUNING problem, not a culling one —
			// use the Lighting panel Bias / Normal Bias sliders instead.
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

		// Terrain Shadow Pipeline
		{
			PipelineConfig terrainShadowConfig;
			terrainShadowConfig.vertexShaderPath = "TerrainShadow.vert";
			terrainShadowConfig.fragmentShaderPath = "Shadow.frag";  // reuse existing
			terrainShadowConfig.useVertexInput = true;
			terrainShadowConfig.topology = PrimitiveTopology::TriangleList;
			terrainShadowConfig.polygonMode = PolygonMode::Fill;
			terrainShadowConfig.cullMode = CullMode::None;
			terrainShadowConfig.frontFace = FrontFace::CounterClockwise;
			terrainShadowConfig.depthTestEnable = true;
			terrainShadowConfig.depthWriteEnable = true;
			terrainShadowConfig.depthCompareOp = CompareOp::LessOrEqual;
			terrainShadowConfig.depthBiasEnable = true;
			terrainShadowConfig.depthBiasConstant = m_ShadowManager->GetTerrainDepthBiasConstant();
			terrainShadowConfig.depthBiasSlope = m_ShadowManager->GetTerrainDepthBiasSlope();
			terrainShadowConfig.useUniformBuffer = true;
			terrainShadowConfig.useHeightmap = true;  // adds set 1 = heightmap layout
			terrainShadowConfig.hasColorAttachment = false;
			terrainShadowConfig.pushConstantSize = sizeof(PushConstantData);
			terrainShadowConfig.pushConstantStages = ShaderStage::Vertex;

			if (m_PipelineAdapter->CreatePipeline(PipelineType::TerrainShadow, terrainShadowConfig))
				LOG_INFO("Terrain shadow pipeline created");
		}

		LOG_INFO("Shadow mapping initialized successfully");
		return true;
	}

	void Renderer::RecordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex)
	{
		// Reset and begin command buffer
		m_Commands->ResetCommandBuffer(frameIndex);
		m_Commands->BeginCommandBuffer(frameIndex);

		VkCommandBuffer profCmd = m_Commands->GetCommandBuffer(frameIndex);
		if (m_GpuProfiler) m_GpuProfiler->BeginFrame(profCmd, frameIndex);

		// =========================================================================
		// COMPUTE PASS - Runs BEFORE any render passes (outside render pass)
		// =========================================================================
		if ((m_ComputeEnabled || m_FireflySystem) && m_ComputeDispatcher)
		{
			uint32_t s = m_GpuProfiler ? m_GpuProfiler->BeginScope(profCmd, "Compute") : UINT32_MAX;
			RecordComputePass(frameIndex);
			if (m_GpuProfiler) m_GpuProfiler->EndScope(profCmd, s);
		}

		// =========================================================================
		// SHADOW PASS
		// =========================================================================
		if (m_ShadowEnabled && m_ShadowManager)
		{
			uint32_t s = m_GpuProfiler ? m_GpuProfiler->BeginScope(profCmd, "Shadow (CSM)") : UINT32_MAX;
			RecordShadowPass(frameIndex);
			if (m_GpuProfiler) m_GpuProfiler->EndScope(profCmd, s);
		}

		// =========================================================================
		// REFLECTION PASS - re-render opaque geometry from the mirror-flipped
		// camera into the reflection target, which the water surface samples in
		// the scene pass below. Only runs when a WaterSystem is registered.
		// =========================================================================
		if (m_WaterSystem)
		{
			uint32_t s = m_GpuProfiler ? m_GpuProfiler->BeginScope(profCmd, "Reflection") : UINT32_MAX;
			RecordReflectionPass(frameIndex);
			if (m_GpuProfiler) m_GpuProfiler->EndScope(profCmd, s);
		}

		// =========================================================================
		// SCENE PASS - all normal geometry, into the offscreen scene-color
		// texture (not the swapchain) so the post-process pass can sample it.
		// =========================================================================
		// Build clear values array
		// Index 0: Color attachment - clear to background color
		// Index 1: Depth attachment - clear to 0.0 for reverse-Z (near=1.0, far=0.0)
		// With MSAA the scene pass has a third (resolve) attachment; its clear
		// value is unused (LOAD_OP_DONT_CARE) but the count must cover it.
		std::array<VkClearValue, 3> clearValues{};
		clearValues[0].color = { {m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a} };
		clearValues[1].depthStencil = { 0.0f, 0 };  // depth = 0.0 (far plane in reverse-Z), stencil = 0
		uint32_t clearValueCount = (m_RenderPasses->GetSampleCount() != VK_SAMPLE_COUNT_1_BIT) ? 3u : 2u;

		uint32_t sceneScope = m_GpuProfiler ? m_GpuProfiler->BeginScope(profCmd, "Scene") : UINT32_MAX;

		m_Commands->BeginRenderPass(frameIndex,
			m_RenderPasses->GetSceneRenderPass(),
			m_RenderPasses->GetSceneFramebuffer(),
			m_Swapchain->GetExtent(),
			clearValues.data(),
			clearValueCount);

		// Execute draw list
		if (!m_FrameDrawList.GetCommands().empty())
		{
			m_Commands->ExecuteDrawList(frameIndex, m_FrameDrawList,
				m_PipelineAdapter.get(),
				m_ViewMatrix, m_ProjectionMatrix);
		}

		m_Commands->EndRenderPass(frameIndex);
		if (m_GpuProfiler) m_GpuProfiler->EndScope(profCmd, sceneScope);

		// =========================================================================
		// POST-PROCESS PASS - samples the scene-color texture, runs FXAA, and
		// writes the actual swapchain image. UI renders after this, directly
		// on the swapchain target, so it isn't blurred by the AA filter.
		// =========================================================================
		uint32_t ppScope = m_GpuProfiler ? m_GpuProfiler->BeginScope(profCmd, "PostProcess+UI") : UINT32_MAX;
		RecordPostProcessPass(frameIndex, imageIndex);
		if (m_GpuProfiler) m_GpuProfiler->EndScope(profCmd, ppScope);

		if (m_GpuProfiler) m_GpuProfiler->EndFrame(frameIndex);

		m_Commands->EndCommandBuffer(frameIndex);
	}

	void Renderer::RecordComputePass(uint32_t frameIndex)
	{
		if (!m_ComputeDispatcher)
		{
			return;
		}

		VkCommandBuffer cmd = m_Commands->GetCommandBuffer(frameIndex);

		if (m_ComputeEnabled)
		{
			// Get compute pipeline and layout
			VkPipeline computePipeline = m_PipelineAdapter->GetVulkanManager()->GetPipeline(PipelineType::Compute);
			VkPipelineLayout computeLayout = m_PipelineAdapter->GetVulkanManager()->GetPipelineLayout(PipelineType::Compute);

			if (computePipeline == VK_NULL_HANDLE || computeLayout == VK_NULL_HANDLE)
			{
				LOG_WARN("Compute pipeline or layout is null");
			}
			else
			{
				// Bind compute pipeline
				m_ComputeDispatcher->BindPipeline(cmd, computePipeline);

				// Bind descriptor set with our storage buffers
				m_ComputeDispatcher->BindDescriptorSet(cmd, computeLayout, 0, m_ComputeTestDescriptorSet);

				// Set push constants with current time
				ComputePushConstants pushData;
				pushData.dataSize = COMPUTE_TEST_ELEMENT_COUNT;
				pushData.time = m_TotalTime;  // Use the renderer's time value

				m_ComputeDispatcher->PushConstants(cmd, computeLayout, &pushData, sizeof(pushData));

				// Dispatch compute work
				// With local_size_x = 64, we need 1 workgroup for 64 elements
				uint32_t groupCountX = ComputeDispatcher::CalculateGroupCount(COMPUTE_TEST_ELEMENT_COUNT, 64);
				m_ComputeDispatcher->Dispatch(cmd, groupCountX, 1, 1);

				// Barrier: ensure compute writes are visible before any graphics work
				m_ComputeDispatcher->ComputeToGraphicsGlobalBarrier(cmd);
			}
		}

		if (m_FireflySystem)
		{
			m_FireflySystem->DispatchCompute(cmd, m_ComputeDispatcher.get(), frameIndex, m_LastDeltaTime);

			m_ComputeDispatcher->ComputeToVertexShaderBarrier(
				cmd, m_FireflySystem->GetAgentBuffer(), m_FireflySystem->GetAgentBufferSize());
		}

		if (m_CloudSystem)
		{
			m_CloudSystem->DispatchRaymarch(cmd, m_ComputeDispatcher.get(), frameIndex);

			m_ComputeDispatcher->ComputeWriteToFragmentSampleBarrier(
				cmd, m_CloudSystem->GetRaymarchResultImage());
		}
	}

	// =====================================================================
	// FIX: RecordShadowPass no longer touches m_FrameUniforms.
	//
	// The old version wrote light matrices into the camera UBO, recorded
	// GPU commands, then restored camera data � all CPU-side. But the GPU
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

		VkExtent2D shadowExtent = m_ShadowManager->GetShadowExtent();

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(shadowExtent.width);
		viewport.height = static_cast<float>(shadowExtent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = shadowExtent;

		VkClearValue depthClear{};
		depthClear.depthStencil = { 1.0f, 0 };  // Standard depth (not reverse-Z)

		// STEP 3: render every cascade into its own array layer. Each cascade has its own
		// framebuffer (one array layer) and its own set-0 UBO (that cascade's light VP).
		// This is ~NUM_CASCADES x the depth-pass draws — watch FPS; per-cascade culling is
		// the step-5 mitigation if it stings.
		for (uint32_t cascade = 0; cascade < NUM_CASCADES; ++cascade)
		{
			VkRenderPassBeginInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = m_ShadowManager->GetShadowRenderPass();
			renderPassInfo.framebuffer = m_ShadowManager->GetShadowFramebuffer(cascade);
			renderPassInfo.renderArea.offset = { 0, 0 };
			renderPassInfo.renderArea.extent = shadowExtent;
			renderPassInfo.clearValueCount = 1;
			renderPassInfo.pClearValues = &depthClear;

			vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			// Bind shadow pipeline
			m_PipelineAdapter->BindPipeline(cmd, PipelineType::Shadow);

			// This cascade's light view/proj (set 0)
			VkDescriptorSet shadowUniformSet = m_DescriptorManager->GetShadowUniformDescriptorSet(frameIndex, cascade);

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

				bool isTerrain = (drawCmd.pipeline == PipelineType::Terrain);

				// Bind appropriate shadow pipeline
				VkPipeline shadowPipeline = isTerrain
					? m_PipelineAdapter->GetVulkanManager()->GetPipeline(PipelineType::TerrainShadow)
					: m_PipelineAdapter->GetVulkanManager()->GetPipeline(PipelineType::Shadow);
				VkPipelineLayout shadowLayout = isTerrain
					? m_PipelineAdapter->GetVulkanManager()->GetPipelineLayout(PipelineType::TerrainShadow)
					: m_PipelineAdapter->GetVulkanManager()->GetPipelineLayout(PipelineType::Shadow);

				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);

				// Bind this cascade's shadow uniform (set 0) - same for both
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					shadowLayout, 0, 1, &shadowUniformSet, 0, nullptr);

				// For terrain: also bind heightmap at set 1
				if (isTerrain)
				{
					if (drawCmd.heightmapDescriptorSet == VK_NULL_HANDLE)
					{
						LOG_WARN("TerrainShadow: heightmapDescriptorSet is null, skipping draw");
						continue;
					}

					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
						shadowLayout, 1, 1, &drawCmd.heightmapDescriptorSet, 0, nullptr);
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
		}
		// No UBO restore needed � each pass has its own dedicated buffer
	}

	// =====================================================================
	// RecordReflectionPass — re-renders the opaque world (Mesh/Terrain/
	// Foliage) from the mirror-flipped camera into the reflection target,
	// which the water surface samples in the scene pass. Reuses the scene's
	// pipelines (the reflection render pass is format/sample-count compatible
	// with the scene pass); the mirror flips triangle winding, which is
	// cancelled by a negative-height viewport so back-face culling stays
	// correct. The water shader undoes the resulting vertical image flip when
	// it samples (sample at v -> 1 - v).
	// =====================================================================
	void Renderer::RecordReflectionPass(uint32_t frameIndex)
	{
		if (!m_WaterSystem || !m_RenderPasses)
		{
			return;
		}

		VkExtent2D extent = m_RenderPasses->GetReflectionExtent();
		if (extent.width == 0 || extent.height == 0)
		{
			return;
		}

		// Clear color (sky/background behind the reflected geometry) + depth.
		// With MSAA the pass has a third (resolve) attachment whose clear value
		// is unused but must be covered by the count.
		std::array<VkClearValue, 3> clearValues{};
		clearValues[0].color = { {m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a} };
		clearValues[1].depthStencil = { 0.0f, 0 };  // reverse-Z far plane
		uint32_t clearValueCount = (m_RenderPasses->GetSampleCount() != VK_SAMPLE_COUNT_1_BIT) ? 3u : 2u;

		m_Commands->BeginRenderPass(frameIndex,
			m_RenderPasses->GetReflectionRenderPass(),
			m_RenderPasses->GetReflectionFramebuffer(),
			extent,
			clearValues.data(),
			clearValueCount);

		// Negative-height viewport flips rasterized winding to cancel the mirror
		// matrix's winding flip — so the scene pipelines' back-face culling stays
		// correct without dedicated reflected-winding pipeline variants. The image
		// is stored vertically flipped as a side effect; the water shader samples
		// with v -> 1 - v to compensate.
		VkCommandBuffer cmd = m_Commands->GetCommandBuffer(frameIndex);
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = static_cast<float>(extent.height);
		viewport.width = static_cast<float>(extent.width);
		viewport.height = -static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkDescriptorSet reflectionUniformSet = m_DescriptorManager->GetReflectionUniformDescriptorSet(frameIndex);
		m_Commands->ExecuteReflectionDrawList(frameIndex, m_FrameDrawList,
			m_PipelineAdapter.get(), reflectionUniformSet);

		m_Commands->EndRenderPass(frameIndex);
	}

	// =====================================================================
	// RecordPostProcessPass — samples the scene-color texture rendered by
	// the scene pass, runs FXAA, and writes the swapchain image. A single
	// fixed full-screen draw, not a DrawList entry, so this is recorded
	// directly here rather than through CommandRecorder's per-pipeline-type
	// binding lists — same precedent as RecordShadowPass/RecordComputePass.
	// UI renders last, in this same pass, directly on the swapchain target.
	// =====================================================================
	void Renderer::RecordPostProcessPass(uint32_t frameIndex, uint32_t imageIndex)
	{
		VkCommandBuffer cmd = m_Commands->GetCommandBuffer(frameIndex);

		VkClearValue clearValue{};
		clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = m_RenderPasses->GetPostProcessRenderPass();
		renderPassInfo.framebuffer = m_RenderPasses->GetPostProcessFramebuffer(imageIndex);
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = m_Swapchain->GetExtent();
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearValue;

		vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkExtent2D extent = m_Swapchain->GetExtent();
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		if (m_PostProcessInputSet != VK_NULL_HANDLE)
		{
			m_PipelineAdapter->BindPipeline(cmd, PipelineType::PostProcess);

			VkPipelineLayout layout = m_PipelineAdapter->GetVulkanManager()->GetPipelineLayout(PipelineType::PostProcess);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
				0, 1, &m_PostProcessInputSet, 0, nullptr);

			int aaEnabled = m_PostProcessAAEnabled ? 1 : 0;
			vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &aaEnabled);

			vkCmdDraw(cmd, 3, 1, 0, 0);
		}

		// Render UI on top — after AA, directly on the swapchain target.
		if (m_UI)
		{
			m_UI->Render(cmd);
		}

		vkCmdEndRenderPass(cmd);
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

		// Recreate framebuffers (also recreates the offscreen scene-color
		// texture the post-process pass samples — its view/sampler handles
		// change, so the descriptor set pointing at them must be re-updated)
		if (!m_RenderPasses->RecreateFramebuffers(vkDevice->GetDevice(), m_Swapchain.get()))
		{
			LOG_ERROR("Failed to recreate framebuffers");
			return false;
		}

		if (m_PostProcessInputSet != VK_NULL_HANDLE)
		{
			m_DescriptorManager->UpdatePostProcessInputSet(m_PostProcessInputSet,
				m_RenderPasses->GetSceneColorImageView(), m_RenderPasses->GetSceneColorSampler());
		}

		// Reflection target was recreated too — re-point the water's sampler set.
		if (m_ReflectionInputSet != VK_NULL_HANDLE)
		{
			m_DescriptorManager->UpdateReflectionInputSet(m_ReflectionInputSet,
				m_RenderPasses->GetReflectionColorImageView(), m_RenderPasses->GetReflectionColorSampler());
		}

		// Recreate the cloud raymarch result image at the new scaled resolution
		if (m_CloudSystem)
		{
			m_CloudSystem->ResizeResultImage(newWidth, newHeight);
		}

		// TODO: Update pipeline viewport/scissor if needed

		LOG_INFO("Swapchain resize handled successfully");
		return true;
	}

	void Renderer::CleanupCompute()
	{
		if (m_NoisePreview)
		{
			delete m_NoisePreview;
			m_NoisePreview = nullptr;
		}

		if (m_NoiseGenerator)
		{
			m_NoiseGenerator->Cleanup();
			m_NoiseGenerator.reset();
		}

		if (m_TestNoise)
		{
			delete m_TestNoise;
			m_TestNoise = nullptr;
		}

		if (m_ComputeDispatcher)
		{
			m_ComputeDispatcher->Cleanup();
			m_ComputeDispatcher.reset();
		}

		// Buffers are owned by ResourceManager, destroy by name
		if (m_Resources)
		{
			m_Resources->DestroyBuffer("ComputeTestInput");
			m_Resources->DestroyBuffer("ComputeTestOutput");
		}

		m_ComputeTestInputBuffer = nullptr;
		m_ComputeTestOutputBuffer = nullptr;
		m_ComputeTestDescriptorSet = VK_NULL_HANDLE;

		m_ComputeEnabled = false;
		LOG_INFO("Compute resources cleaned up");
	}

	void Renderer::SetShadowResolution(uint32_t resolution)
	{
		if (!m_ShadowManager || !m_DescriptorManager) return;
		if (resolution == m_ShadowManager->GetConfig().resolution) return;

		if (!m_ShadowManager->Resize(resolution))  // waits idle + recreates texture/views/framebuffers
		{
			LOG_ERROR("Failed to resize shadow map to {}", resolution);
			return;
		}

		// Resize only refreshed ShadowMapManager's own descriptor copies; repoint the
		// descriptor-manager sets the main pass actually binds at the recreated array view.
		for (uint32_t i = 0; i < FrameSyncManager::MAX_FRAMES_IN_FLIGHT; ++i)
		{
			m_DescriptorManager->UpdateShadowSet(
				i,
				m_ShadowManager->GetShadowMapView(),
				m_ShadowManager->GetShadowSampler());
		}
		LOG_INFO("Shadow map resolution set to {}x{}", resolution, resolution);
	}

	uint32_t Renderer::GetShadowResolution() const
	{
		return m_ShadowManager ? m_ShadowManager->GetConfig().resolution : 0;
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

		const float bias       = m_ShadowConfig.bias;
		const float normalBias = m_ShadowConfig.normalBias;
		const float shadowDist = m_ShadowConfig.shadowDistance;
		const float lambda     = m_ShadowConfig.splitLambda;
		const float extrude    = m_ShadowConfig.casterExtrude;
		const float resolution = static_cast<float>(m_ShadowManager->GetConfig().resolution);

		// Light basis: position.xyz = direction the light is SHINING (e.g. (0,-1,0) = down).
		glm::vec3 lightDir = glm::normalize(glm::vec3(primaryLight.position));
		glm::vec3 upRef = glm::vec3(0.0f, 1.0f, 0.0f);
		if (std::abs(glm::dot(lightDir, upRef)) > 0.99f)
			upRef = glm::vec3(0.0f, 0.0f, 1.0f);

		// Recover camera basis + projection params from the matrices the app fed us, so the
		// shadow system needs no extra plumbing. Projection is infinite reverse-Z (Camera.cpp):
		//   proj[1][1] = -1/tan(fovY/2)  (Vulkan Y-flip baked in),  proj[3][2] = near.
		glm::mat4 invView  = glm::inverse(m_ViewMatrix);
		glm::vec3 camPos   = glm::vec3(invView[3]);
		glm::vec3 camRight = glm::normalize(glm::vec3(invView[0]));
		glm::vec3 camUp    = glm::normalize(glm::vec3(invView[1]));
		glm::vec3 camFwd   = glm::normalize(-glm::vec3(invView[2]));

		float tanHalfFovY = -1.0f / m_ProjectionMatrix[1][1];
		float aspect      = -m_ProjectionMatrix[1][1] / m_ProjectionMatrix[0][0];
		float camNear     = m_ProjectionMatrix[3][2];
		if (camNear <= 0.0f) camNear = 0.1f;

		m_ShadowCenter = camPos;  // keep member meaningful for diagnostics

		// --- Practical (PSSM) split distances over [camNear, shadowDist], view-space ---
		float splitFar[NUM_CASCADES];
		float ratio = shadowDist / camNear;
		for (uint32_t c = 0; c < NUM_CASCADES; ++c)
		{
			float p = static_cast<float>(c + 1) / static_cast<float>(NUM_CASCADES);
			float logSplit = camNear * std::pow(ratio, p);
			float uniSplit = camNear + (shadowDist - camNear) * p;
			splitFar[c] = lambda * logSplit + (1.0f - lambda) * uniSplit;
		}

		for (uint32_t c = 0; c < NUM_CASCADES; ++c)
		{
			float sliceNear = (c == 0) ? camNear : splitFar[c - 1];
			float sliceFar  = splitFar[c];

			// 8 world-space corners of this frustum slice.
			glm::vec3 corners[8];
			int idx = 0;
			for (int fi = 0; fi < 2; ++fi)
			{
				float d = (fi == 0) ? sliceNear : sliceFar;
				float halfH = d * tanHalfFovY;
				float halfW = halfH * aspect;
				glm::vec3 cc = camPos + camFwd * d;
				corners[idx++] = cc - camRight * halfW - camUp * halfH;
				corners[idx++] = cc + camRight * halfW - camUp * halfH;
				corners[idx++] = cc - camRight * halfW + camUp * halfH;
				corners[idx++] = cc + camRight * halfW + camUp * halfH;
			}

			// Bounding sphere of the slice — rotation-invariant, so the ortho size is constant
			// as the camera turns. That stability is what makes the texel snap below kill the
			// edge shimmer instead of merely reducing it.
			glm::vec3 center(0.0f);
			for (int i = 0; i < 8; ++i) center += corners[i];
			center /= 8.0f;
			float radius = 0.0f;
			for (int i = 0; i < 8; ++i)
				radius = std::max(radius, glm::length(corners[i] - center));
			radius = std::ceil(radius * 16.0f) / 16.0f;

			// Light view aimed at the sphere centre, pulled back so off-frustum occluders
			// toward the light still cast (casterExtrude = pancaking margin).
			glm::vec3 eye = center - lightDir * (radius + extrude);
			glm::mat4 lightView = glm::lookAt(eye, center, upRef);

			float zFar = 2.0f * radius + extrude;
			glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, zFar);
			lightProj[1][1] *= -1.0f;                              // Vulkan Y-flip
			lightProj[2][2] *= 0.5f;                               // GL [-1,1] -> VK [0,1] depth
			lightProj[3][2] = lightProj[3][2] * 0.5f + 0.5f;

			// Texel-snap in NDC: round the projected world origin to the shadow-map grid so
			// the sampled texels don't crawl as the camera translates.
			glm::mat4 shadowMatrix = lightProj * lightView;
			glm::vec2 origin  = glm::vec2(shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)) * (resolution * 0.5f);
			glm::vec2 rounded = glm::round(origin);
			glm::vec2 offset  = (rounded - origin) * (2.0f / resolution);
			lightProj[3][0] += offset.x;
			lightProj[3][1] += offset.y;

			glm::mat4 lightVP = lightProj * lightView;

			m_CurrentLightingData.shadowData.lightSpaceMatrix[c] = lightVP;
			m_CurrentLightingData.shadowData.cascadeSplits[c] = sliceFar;  // view-depth split (shader selection)
			m_CurrentLightingData.shadowData.cascadeRadii[c]  = radius;    // ortho half-size (shader bias scaling)

			m_ShadowFrameData[c].view = lightView;
			m_ShadowFrameData[c].proj = lightProj;
			m_ShadowFrameData[c].time = glm::vec4(m_TotalTime, 0.0f, 0.0f, 0.0f);
			m_ShadowFrameData[c].cameraPos = glm::vec4(eye, 1.0f);

			if (c == 0)
			{
				// Base bias expressed in cascade 0's NDC depth range; the shader scales it per
				// cascade by cascadeRadii ratio (coarser far cascades need proportionally more).
				float ndcBias = (zFar > 0.0f) ? (bias / zFar) : bias;
				m_CurrentLightingData.shadowData.shadowParams = glm::vec4(
					ndcBias, normalBias, m_DebugCascadeTint ? 1.0f : 0.0f, 1.0f);
				m_CurrentLightingData.shadowData.extraParams = glm::vec4(
					m_ShadowConfig.cascadeBlend, 0.0f, 0.0f, 0.0f);
			}
		}
	}

} // namespace Nightbloom