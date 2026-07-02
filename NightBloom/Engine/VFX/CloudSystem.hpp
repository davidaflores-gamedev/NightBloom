//------------------------------------------------------------------------------
// CloudSystem.hpp
//
// Volumetric cloud layer: raymarches a horizontal sky-volume slab sampling
// scrolling 3D Perlin-Worley noise (shape) eroded by a higher-frequency
// Worley detail texture. Mirrors TerrainSystem's heightmap-generation
// pattern — shape/detail noise textures are generated rarely (via
// NoiseTextureGenerator), not per-frame; movement comes from scrolling the
// sample coordinate by a wind/time uniform updated every frame.
//
// Performance: the raymarch itself runs in a compute shader
// (CloudRaymarch.comp) at a LOWER resolution (see m_ResolutionScale) than
// the screen, writing into m_RaymarchResult. The graphics PipelineType::
// Clouds pass then just samples that small result (hardware bilinear
// upscale) and composites it — occlusion against terrain/opaque geometry is
// still handled entirely by the normal full-resolution GPU depth test in
// that graphics pass (fixed "at infinity" depth output, depth-test on,
// write off), completely unchanged from before this optimization — only the
// expensive raymarch content moved to compute, not the occlusion decision.
//
// Usage:
//   CloudSystem clouds;
//   clouds.Initialize(renderer);
//   clouds.Regenerate(desc);           // rare — rebuilds shape/detail textures
//   renderer->SetCloudSystem(&clouds); // Renderer calls UpdateParams() + DispatchRaymarch() per frame
//   // Per frame (after BuildDrawList):
//   clouds.SubmitDraw(drawList);
//   clouds.Shutdown();
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/NoiseTextureGenerator.hpp"
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace Nightbloom
{
	class Renderer;
	class ResourceManager;
	class VulkanDescriptorManager;
	class VulkanTexture;
	class VulkanBuffer;
	class ComputeDispatcher;

	struct CloudDesc
	{
		// Shape (low-frequency Perlin-Worley) and detail (higher-frequency
		// Worley erosion) noise textures. NoiseTextureDesc.depth must be > 1
		// for these (real 3D textures, unlike Terrain's flat heightmap).
		NoiseTextureDesc shapeNoise;
		NoiseTextureDesc detailNoise;

		// World-space vertical bounds of the cloud layer slab
		float layerMinY = 800.0f;
		float layerMaxY = 1400.0f;

		// Wind direction (normalized internally) and speed, world units/sec
		glm::vec3 windDirection = glm::vec3(1.0f, 0.0f, 0.3f);
		float windSpeed = 8.0f;

		// World-position-to-noise-UV scale (smaller = larger cloud features)
		float shapeScale = 0.0015f;
		float detailScale = 0.012f;
		float detailStrength = 0.35f; // how strongly detail erodes shape

		float coverage = 0.45f;          // 0 = no clouds, 1 = fully overcast
		float densityMultiplier = 1.0f;
		float extinctionCoefficient = 1.2f;
		float hgAnisotropy = 0.2f;       // Henyey-Greenstein phase g, forward-scatter bias
		int   stepCount = 64;            // was 80 — ~20% fewer raymarch steps; the
		                                 // dithered start + early-out hide the loss

		// Raymarch result image resolution = swapchain extent * this scale.
		// 0.45 (~20% of full pixel count) trades a little upscale softness — fine
		// for soft clouds — for a meaningful raymarch cost cut. Raise toward 1.0
		// for crisper clouds, lower for more speed.
		float resolutionScale = 0.45f;
	};

	// Matches CloudParamsUBO in CloudRaymarch.comp exactly (std140, 4x vec4 = 64 bytes)
	struct CloudParamsData
	{
		glm::vec4 layerBounds = glm::vec4(800.0f, 1400.0f, 0.0f, 0.0f); // x=minY, y=maxY
		glm::vec4 wind        = glm::vec4(8.0f, 0.0f, 2.4f, 0.0f);      // xyz=wind dir*speed, w=totalTime
		glm::vec4 shape       = glm::vec4(0.0015f, 0.012f, 0.35f, 0.45f); // x=shapeScale,y=detailScale,z=detailStrength,w=coverage
		glm::vec4 density     = glm::vec4(1.0f, 1.2f, 0.2f, 80.0f);      // x=densityMul,y=extinction,z=hgG,w=stepCount
	};

	class CloudSystem
	{
	public:
		CloudSystem() = default;
		~CloudSystem() = default;

		CloudSystem(const CloudSystem&) = delete;
		CloudSystem& operator=(const CloudSystem&) = delete;

		bool Initialize(Renderer* renderer);
		bool Regenerate(const CloudDesc& desc);
		void Shutdown();

		// Advances wind-scroll time and uploads this frame's params UBO.
		// Called by Renderer::BeginFrame() every frame (via SetCloudSystem).
		void UpdateParams(uint32_t frameIndex, float deltaTime);

		// Dispatches the low-res raymarch compute pass. Called by
		// Renderer::RecordComputePass() every frame (via SetCloudSystem),
		// mirroring FireflySystem's compute dispatch pattern. Caller is
		// responsible for the compute->fragment barrier afterward (same
		// split of responsibility as FireflySystem's compute->vertex barrier).
		void DispatchRaymarch(VkCommandBuffer cmd, ComputeDispatcher* dispatcher, uint32_t frameIndex);

		// Second raymarch for the water reflection: same shader, same noise/
		// params, but fed the mirror-flipped reflection camera at set 0
		// (reflectionUniformSet) and writing a SEPARATE low-res result image.
		// Only dispatched by Renderer::RecordComputePass when a WaterSystem is
		// registered. The reflection pass then composites this result into the
		// reflection target (see GetReflectionResultSet), so the water samples
		// clouds "for free" with no Water.frag change. Caller owns the
		// compute->fragment barrier afterward, same as DispatchRaymarch.
		void DispatchReflectionRaymarch(VkCommandBuffer cmd, ComputeDispatcher* dispatcher,
			uint32_t frameIndex, VkDescriptorSet reflectionUniformSet);

		void SubmitDraw(DrawList& drawList) const;

		bool IsReady() const { return m_Ready; }

		CloudDesc& GetDesc() { return m_CurrentDesc; }
		const CloudDesc& GetDesc() const { return m_CurrentDesc; }

		// Recreates the low-res result image at the current resolutionScale
		// against new dimensions — called on window resize, or when the
		// panel changes resolutionScale. viewportWidth/Height are the full
		// (unscaled) swapchain dimensions.
		bool ResizeResultImage(uint32_t viewportWidth, uint32_t viewportHeight);

		VkImage GetRaymarchResultImage() const;

		// Reflection variant of the above — the image the mirror-camera raymarch
		// writes, and the descriptor set the reflection pass's Clouds composite
		// samples (set 0 of the Clouds pipeline, same slot as m_ResultDescriptorSet).
		VkImage GetReflectionResultImage() const;
		VkDescriptorSet GetReflectionResultSet() const { return m_ReflectionResultSet; }

	private:
		void DestroyNoiseTextures();
		void DestroyResultImage();
		bool CreateComputePipeline();

		Renderer* m_Renderer = nullptr;
		ResourceManager* m_Resources = nullptr;
		VulkanDescriptorManager* m_DescriptorManager = nullptr;

		VulkanTexture* m_ShapeTexture = nullptr;  // caller-owned, like Terrain's heightmap
		VulkanTexture* m_DetailTexture = nullptr;
		VulkanTexture* m_RaymarchResult = nullptr; // caller-owned, low-res compute output
		VulkanTexture* m_ReflectionResult = nullptr; // caller-owned, low-res mirror-camera output for water reflection

		VulkanBuffer* m_ParamsBuffers[2] = { nullptr, nullptr }; // double-buffered, owned by ResourceManager

		VkDescriptorSet m_ResultDescriptorSet = VK_NULL_HANDLE; // graphics composite pass's only input (set 1)
		VkDescriptorSet m_OutputImageSet = VK_NULL_HANDLE;      // compute pass's output binding (set 3)
		VkDescriptorSet m_ReflectionResultSet = VK_NULL_HANDLE;   // reflection composite input (set 0 of Clouds)
		VkDescriptorSet m_ReflectionOutputImageSet = VK_NULL_HANDLE; // reflection raymarch output binding (set 3)

		// Raw compute pipeline - owned directly, mirrors FireflySystem's
		// pattern (a continuous per-frame simulation, not a generic
		// PipelineType graphics draw).
		VkPipeline       m_RaymarchPipeline = VK_NULL_HANDLE;
		VkPipelineLayout m_RaymarchPipelineLayout = VK_NULL_HANDLE;

		uint32_t m_ResultWidth = 0;
		uint32_t m_ResultHeight = 0;
		bool m_ResultImageEverWritten = false; // first dispatch transitions from UNDEFINED, not SHADER_READ_ONLY_OPTIMAL
		bool m_ReflectionResultEverWritten = false; // same, for the reflection result image

		CloudDesc m_CurrentDesc;
		float m_TotalTime = 0.0f;
		bool m_Ready = false;
	};

} // namespace Nightbloom
