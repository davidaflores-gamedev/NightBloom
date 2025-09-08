//------------------------------------------------------------------------------
// ResourceManager.hpp
//
// Manages GPU resources (buffers, textures, samplers)
// Handles resource creation, destruction, and tracking
//------------------------------------------------------------------------------
#pragma once

#include <Vulkan/vulkan.h>
#include <memory>
#include <unordered_map>
#include <string>
#include <cstdint>

// Full definitions
#include "Engine/Renderer/Vulkan/VulkanCommandPool.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Renderer/Vulkan/VulkanShader.hpp"

namespace Nightbloom
{
	// Forward Declarations
	class VulkanDevice;
	class VulkanMemoryManager;
	class Buffer;

	class ResourceManager
	{
	public:
		ResourceManager() = default;
		~ResourceManager() = default;

		// Lifecycle
		bool Initialize(VulkanDevice* device, VulkanMemoryManager* memoryManager);
		void Cleanup();

		// Buffer management
		VulkanBuffer * CreateBuffer(const std::string & name, size_t size, VkBufferUsageFlags usage, bool hostVisible = false);
		VulkanBuffer* GetBuffer(const std::string& name);
		void DestroyBuffer(const std::string& name);

		// Shader management
		VulkanShader* LoadShader(const std::string& name, ShaderStage stage,
			const std::string& filename);
		VulkanShader* GetShader(const std::string& name);
		void DestroyShader(const std::string& name);
		void DestroyAllShaders();

		// Test Geometry (temporary - will be replaced with proper mesh loading)
		bool CreateTestCube();
		Buffer* GetTestVertexBuffer() const;
		Buffer* GetTestIndexBuffer() const;
		uint32_t GetTestIndexCount() const { return m_TestIndexCount; }

		// Upload utilities
		bool UploadBufferData(VulkanBuffer* buffer, const void* data, size_t size);

		// Future: Texture management
		// VulkanTexture* CreateTexture(const std::string& name, const TextureDesc& desc);
		// VulkanTexture* LoadTexture(const std::string& filepath);
		// VulkanTexture* GetTexture(const std::string& name);
		// void DestroyTexture(const std::string& name);

		// Future: Sampler management
		// VkSampler CreateSampler(const SamplerDesc& desc);
		// void DestroySampler(VkSampler sampler);

		// Resource statistics
		size_t GetTotalBufferMemory() const;
		size_t GetBufferCount() const { return m_Buffers.size(); }

	private:
		VulkanDevice* m_Device = nullptr;
		VulkanMemoryManager* m_MemoryManager = nullptr;
		std::unique_ptr<VulkanCommandPool> m_TransferCommandPool;  // For staging uploads

		// Resource storage
		std::unordered_map<std::string, std::unique_ptr<VulkanBuffer>> m_Buffers;
		std::unordered_map<std::string, std::unique_ptr<VulkanShader>> m_Shaders;
		// Future: std::unordered_map<std::string, std::unique_ptr<VulkanTexture>> m_Textures;
		// Future: std::vector<VkSampler> m_Samplers;

		// Test resources (temporary)
		uint32_t m_TestIndexCount = 0;

		// Helper methods
		//VkCommandBuffer BeginSingleTimeCommands();
		//void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

		// Prevent copying
		ResourceManager(const ResourceManager&) = delete;
		ResourceManager& operator=(const ResourceManager&) = delete;
	};

} // namespace Nightbloom