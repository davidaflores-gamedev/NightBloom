//------------------------------------------------------------------------------
// NoiseTextureGenerator.hpp
//
// Generates procedural noise textures on the GPU using compute shaders.
// Supports Perlin FBM, Worley FBM, and Perlin-Worley blend — all the noise
// types needed for volumetric cloud rendering.
//
// Generated textures are 3D (VK_IMAGE_TYPE_3D). For "2D" noise such as a
// weather map, set depth = 1 and sample using sampler3D at z = 0.5 in your
// fragment/raymarch shader.
//
// Usage:
//   NoiseTextureGenerator gen;
//   gen.Initialize(device, memoryManager, commandPool, descriptorManager);
//
//   NoiseTextureDesc desc;
//   desc.width = desc.height = desc.depth = 128;
//   desc.noiseType = NoiseType::PerlinWorley;
//   desc.octaves = 4;
//
//   VulkanTexture* cloudShape = gen.Generate(desc, dispatcher);
//   // cloudShape is sampler-ready; caller owns and must delete it on shutdown
//------------------------------------------------------------------------------
#pragma once

#include <vulkan/vulkan.h>
#include <string>

namespace Nightbloom
{
	class VulkanDevice;
	class VulkanMemoryManager;
	class VulkanCommandPool;
	class VulkanDescriptorManager;
	class ComputeDispatcher;
	class VulkanTexture;

	// =========================================================================
	// NoiseType
	// =========================================================================
	enum class NoiseType
	{
		Perlin = 0,    // FBM Perlin noise,          output [0, 1]
		Worley = 1,    // Inverted FBM Worley,        output [0, 1]  (bright = cell centers)
		PerlinWorley = 2,   // Perlin base eroded by Worley — primary cloud shape channel


	};

	// =========================================================================
	// NoiseTextureDesc
	// =========================================================================
	struct NoiseTextureDesc
	{
		uint32_t  width = 128;
		uint32_t  height = 128;
		uint32_t  depth = 128;            // Must be >= 1. Use 1 for "flat" 3D (samples as 2D at z=0.5)

		NoiseType noiseType = NoiseType::Perlin;

		uint32_t  octaves = 4;              // Number of FBM octaves
		float     frequency = 4.0f;           // Base frequency (tiles across the texture this many times)
		float     persistence = 0.5f;           // Amplitude multiplier per octave (< 1 = each octave quieter)
		float     lacunarity = 2.0f;           // Frequency multiplier per octave (> 1 = finer detail per octave)
		uint32_t  seed = 42;             // Random seed — different seeds shift the noise field

		std::string debugName = "NoiseTexture";
	};

	// =========================================================================
	// NoiseTextureGenerator
	// =========================================================================
	class NoiseTextureGenerator
	{
	public:
		NoiseTextureGenerator();
		~NoiseTextureGenerator();

		bool Initialize(VulkanDevice* device, VulkanMemoryManager* memoryManager, VulkanCommandPool* commandPool, VulkanDescriptorManager* descriptorManager);

		void Cleanup();

		// Generate a noise texture and return it in a sampler-ready state.
		// The caller takes ownership of the returned texture and is responsible
		// for deleting it at shutdown (or whenever the texture is no longer needed).
		// Returns nullptr if generation fails.
		VulkanTexture* Generate(const NoiseTextureDesc& desc, ComputeDispatcher* dispatcher);

	private:
		// ------------------------------------------------------------------
		// Push constants layout — must match noise.comp exactly
		// ------------------------------------------------------------------
		struct NoisePushConstants
		{
			uint32_t width;
			uint32_t height;
			uint32_t depth;
			uint32_t seed;
			uint32_t octaves;
			float    frequency;
			float    persistence;
			float    lacunarity;
			uint32_t noiseType;   // Maps to NoiseType enum value
		};

		bool CreateNoisePipeline();

		bool CreateNoisePipeline2D();

		VulkanDevice* m_Device = nullptr;
		VulkanMemoryManager* m_MemoryManager = nullptr;
		VulkanCommandPool* m_CommandPool = nullptr;
		VulkanDescriptorManager* m_DescriptorManager = nullptr;

		// The noise pipeline is owned directly by the generator, not through
		// PipelineType enum, because it's a one-shot/utility pipeline rather
		// than a per-frame rendering pipeline.
		VkPipeline                m_Pipeline = VK_NULL_HANDLE;
		VkPipelineLayout          m_PipelineLayout = VK_NULL_HANDLE;

		VkPipeline       m_Pipeline2D = VK_NULL_HANDLE;
		VkPipelineLayout m_PipelineLayout2D = VK_NULL_HANDLE;

		bool m_Initialized = false;
	};
}