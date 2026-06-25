//------------------------------------------------------------------------------
// Renderer.hpp (REFACTORED VERSION)
//
// Orchestrates rendering components
// Much simpler than before - delegates to specialized managers
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/Vulkan/VulkanCommon.hpp"
#include "Engine/Renderer/PipelineInterface.hpp"
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include "Engine/Renderer/Light.hpp"
#include <array>
#include <memory>
#include <glm/glm.hpp>
#include <cstdint>

namespace Nightbloom
{
	// Forward declarations
	class RenderDevice;
	class VulkanSwapchain;
	class VulkanMemoryManager;
	class VulkanPipelineAdapter;
	class Buffer;
	class UniformBuffer;
	//class Shader;
	class DrawList;
	class IPipelineManager;

	// Component forward declarations
	class FrameSyncManager;
	class RenderPassManager;
	class CommandRecorder;
	class ResourceManager;
	class VulkanDescriptorManager;
	class UIManager;
	class ComputeDispatcher;
	class NoiseTextureGenerator;
	struct NoiseTextureDesc;
	class ShadowMapManager;
	class FireflySystem;
	class CloudSystem;
	class WaterSystem;

	//texture include?
	class VulkanTexture;

	class Renderer
	{
	public:
		Renderer();
		~Renderer();

		// Lifecycle
		bool Initialize(void* windowHandle, uint32_t width, uint32_t height);
		void Shutdown();

		// Frame operations
		void BeginFrame();
		void EndFrame();
		void FinalizeFrame();  // Prepare and record commands

		// Drawing interface
		void SubmitDrawList(const DrawList& drawList);
		void SetViewMatrix(const glm::mat4& view) { m_ViewMatrix = view; }
		void SetProjectionMatrix(const glm::mat4& proj) { m_ProjectionMatrix = proj; }
		void SetCameraPosition(const glm::vec3& pos) { m_CameraPosition = pos; }
		void SetLightingData(const SceneLightingData& data) { m_CurrentLightingData = data; }

		// Clear screen (for when draw list is empty)
		void Clear(float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f);

		// Resource access (temporary - for editor compatibility)
		Buffer* GetTestVertexBuffer() const;
		Buffer* GetTestIndexBuffer() const;
		uint32_t GetTestIndexCount() const;

		Buffer* GetGroundPlaneVertexBuffer() const;
		Buffer* GetGroundPlaneIndexBuffer() const;
		uint32_t GetGroundPlaneIndexCount() const;

		void TestShaderClass();

		void RunComputeTest();
		void PrintComputeTestResults();

		// Noise access
		VulkanTexture* GetNoisePreview() const { return m_NoisePreview; }
		bool RegenerateNoisePreview(const NoiseTextureDesc& desc);
		NoiseTextureGenerator* GetNoiseGenerator() const { return m_NoiseGenerator.get(); }
		ComputeDispatcher* GetComputeDispatcher() const { return m_ComputeDispatcher.get(); }

		bool LoadShaders();

		// System access
		RenderDevice* GetDevice() const { return m_Device.get(); }
		VkDevice GetVkDevice() const; // raw Vulkan device handle, for systems building their own pipelines (e.g. FireflySystem)
		VulkanMemoryManager* GetMemoryManager() const { return m_MemoryManager.get(); } // for systems constructing their own VulkanTexture directly (e.g. CloudSystem's resizable result image)
		IPipelineManager* GetPipelineManager() const { return (IPipelineManager*)(m_PipelineAdapter.get()); };
		ResourceManager* GetResourceManager() const { return m_Resources.get(); }
		VulkanDescriptorManager* GetDescriptorManager() { return m_DescriptorManager.get(); }
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		// FireflySystem dispatches its compute simulation here every frame,
		// before the main render pass, via RecordComputePass(). Not owned —
		// caller (e.g. FireflyPanel) manages its lifetime.
		void SetFireflySystem(FireflySystem* system) { m_FireflySystem = system; }

		// CloudSystem's per-frame params UBO is updated here every frame
		// (BeginFrame already knows the current frame index and delta time
		// for the other per-frame UBOs). Not owned — caller (CloudPanel)
		// manages its lifetime.
		void SetCloudSystem(CloudSystem* system) { m_CloudSystem = system; }

		// WaterSystem registers here so the reflection pass can mirror the camera
		// across the water plane's Y and re-render the scene into the reflection
		// target each frame. Setting null disables the reflection pass. Not owned
		// — caller (WaterEditorPanel) manages its lifetime.
		void SetWaterSystem(WaterSystem* system) { m_WaterSystem = system; }

		// Pipeline operations (temporary - for testing)
		void TogglePipeline();
		void ReloadShaders();

		// Shadow controls
		bool IsShadowEnabled() const { return m_ShadowEnabled; }
		bool IsPostProcessAAEnabled() const { return m_PostProcessAAEnabled; }
		void SetPostProcessAAEnabled(bool enabled) { m_PostProcessAAEnabled = enabled; }
		void SetShadowEnabled(bool enabled) { m_ShadowEnabled = enabled; }
		void SetShadowCenter(const glm::vec3& center) { m_ShadowCenter = center; }
		void SetShadowConfig(const ShadowConfig& config) { m_ShadowConfig = config; }
		const ShadowConfig& GetShadowConfig() const { return m_ShadowConfig; }

		// Status
		bool IsInitialized() const { return m_Initialized; }
		bool IsFrameValid() const { return m_FrameValid; }

		void WaitForIdle()
		{
			if (m_Device)
			{
				m_Device->WaitForIdle();
			}
		}

	private:
		// Core systems (owned by Renderer)
		std::unique_ptr<RenderDevice> m_Device;
		std::unique_ptr<VulkanSwapchain> m_Swapchain;
		std::unique_ptr<VulkanMemoryManager> m_MemoryManager;
		std::unique_ptr<VulkanPipelineAdapter> m_PipelineAdapter;

		// Component managers (owned by Renderer)
		std::unique_ptr<FrameSyncManager> m_FrameSync;
		std::unique_ptr<RenderPassManager> m_RenderPasses;
		std::unique_ptr<CommandRecorder> m_Commands;
		std::unique_ptr<ResourceManager> m_Resources;
		std::unique_ptr<VulkanDescriptorManager> m_DescriptorManager;
		std::unique_ptr<UIManager> m_UI;
		std::unique_ptr<ComputeDispatcher> m_ComputeDispatcher;
		std::unique_ptr<NoiseTextureGenerator> m_NoiseGenerator;
		std::unique_ptr<ShadowMapManager> m_ShadowManager;

		// Frame state
		DrawList m_FrameDrawList;
		glm::mat4 m_ViewMatrix = glm::mat4(1.0f);
		glm::mat4 m_ProjectionMatrix = glm::mat4(1.0f);
		glm::vec3 m_CameraPosition = glm::vec3(0.0f);
		uint32_t m_CurrentImageIndex = 0;
		glm::vec4 m_ClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

		// Camera uniform buffers (set 0 in main pass)
		std::array<VulkanBuffer*, 2> m_FrameUniforms{};  // 2 = MAX_FRAMES_IN_FLIGHT
		FrameUniformData m_CurrentFrameData;

		// Lighting uniform buffers (set 2)
		std::array<VulkanBuffer*, 2> m_LightingUniforms{};
		SceneLightingData m_CurrentLightingData;

		// Shadow uniform buffers (set 0 in shadow pass - light's view/proj)
		std::array<VulkanBuffer*, 2> m_ShadowUniforms{};
		FrameUniformData m_ShadowFrameData;

		// Reflection uniform buffers (set 0 in the planar-reflection pass - the
		// mirror-flipped camera's view/proj). Same double-buffered pattern as the
		// shadow UBO above.
		std::array<VulkanBuffer*, 2> m_ReflectionUniforms{};
		FrameUniformData m_ReflectionFrameData;

		// Reflection target sampler descriptor set (Water set 2). Allocated once
		// in InitializePipelines; re-pointed in HandleSwapchainResize since the
		// reflection target is recreated then (like m_PostProcessInputSet).
		VkDescriptorSet m_ReflectionInputSet = VK_NULL_HANDLE;

		// Post-process (FXAA) — samples the scene-color texture RenderPassManager
		// owns; set is allocated/updated once in InitializePipelines and
		// re-updated in HandleSwapchainResize since the texture is recreated then.
		VkDescriptorSet m_PostProcessInputSet = VK_NULL_HANDLE;

		//Compute support
		bool m_ComputeEnabled = false;
		VkDescriptorSet m_ComputeTestDescriptorSet = VK_NULL_HANDLE;
		VulkanBuffer* m_ComputeTestInputBuffer;
		VulkanBuffer* m_ComputeTestOutputBuffer;
		static constexpr uint32_t COMPUTE_TEST_ELEMENT_COUNT = 64;

		// Compute push constants
		struct ComputePushConstants
		{
			uint32_t dataSize;
			float time;
		};

		// Noise Generator test texture
		VulkanTexture* m_TestNoise = nullptr;
		VulkanTexture* m_NoisePreview = nullptr;  // 2D (depth=1), displayable in ImGui

		// Shadow state
		bool m_ShadowEnabled = true;
		bool m_PostProcessAAEnabled = true;
		glm::vec3 m_ShadowCenter = glm::vec3(0.0f);
		ShadowConfig m_ShadowConfig;

		float m_TotalTime = 0.0f;  // Track time for shaders
		float m_LastDeltaTime = 0.0f;  // Frame-to-frame delta, for systems like FireflySystem

		FireflySystem* m_FireflySystem = nullptr; // not owned
		CloudSystem* m_CloudSystem = nullptr; // not owned
		WaterSystem* m_WaterSystem = nullptr; // not owned

		// Testing state (temporary)
		PipelineType m_CurrentPipeline = PipelineType::Mesh;

		// Status
		bool m_Initialized = false;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		void* m_WindowHandle = nullptr;
		bool m_FrameValid = false;

		// Private initialization helpers
		bool InitializeCore();
		bool InitializeComponents();
		bool InitializePipelines();
		bool InitializeCompute();
		bool InitializeShadowMapping();

		// Helper methods
		void RecordCommandBuffer(uint32_t frameIndex, uint32_t imageIndex);
		void RecordComputePass(uint32_t frameIndex);
		void RecordShadowPass(uint32_t frameIndex);
		void RecordReflectionPass(uint32_t frameIndex);
		void RecordPostProcessPass(uint32_t frameIndex, uint32_t imageIndex);
		void UpdateShadowMatrices();
		bool HandleSwapchainResize();

		void CleanupCompute();

		// Prevent copying
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;
	};

} // namespace Nightbloom