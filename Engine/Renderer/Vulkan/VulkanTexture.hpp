//------------------------------------------------------------------------------
// VulkanTexture.hpp
//
// Vulkan implementation of the Texture interface
//------------------------------------------------------------------------------
#pragma once
#include "Engine/Renderer/RenderDevice.hpp"
#include <vulkan/vulkan.h>
#include "Engine/Renderer/Vulkan/VulkanMemoryManager.hpp"

namespace Nightbloom
{
	class VulkanDevice;
	class VulkanMemoryManager;
	class VulkanCommandPool;
	class VulkanDescriptorManager;

	class VulkanTexture : public Texture
	{
	public:
		VulkanTexture(VulkanDevice* device, VulkanMemoryManager* memoryManager);
		~VulkanTexture() override;

		bool Initialize(const TextureDesc& desc);

		bool UploadData(const void* data, size_t, VulkanCommandPool* cmdPool);

		bool CreateDescriptorSet(VulkanDescriptorManager* descriptorManager);

		// Texture interface implementation
		uint32_t GetWidth() const override { return m_Width; }
		uint32_t GetHeight() const override { return m_Height; }
		uint32_t GetDepth() const override { return m_Depth; }
		uint32_t GetMipLevels() const override { return m_MipLevels; }
		uint32_t GetArrayLayers() const override { return m_ArrayLayers; }
		TextureFormat GetFormat() const override { return m_Format; }
		TextureUsage GetUsage() const override { return m_Usage; }

		VkImage GetImage() const { return m_ImageAllocation ? m_ImageAllocation->image : VK_NULL_HANDLE; }
		VkImageView GetImageView() const { return m_ImageView; }
		VkSampler GetSampler() const { return m_Sampler; }
		VkImageLayout GetCurrentLayout() const { return m_CurrentLayout; }
		VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }
		bool HasDescriptorSet() const { return m_DescriptorSet != VK_NULL_HANDLE; }

		void TransitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout);

	private:
		void Cleanup();
		bool CreateImage();
		bool CreateImageView();
		bool CreateSampler();
		void GenerateMipmaps(VkCommandBuffer cmd);
		static uint32_t CalculateMipLevels(uint32_t width, uint32_t heights);

		VkFormat ConvertToVkFormat(TextureFormat format);

	private:
		VulkanDevice* m_Device = nullptr;
		VulkanMemoryManager* m_MemoryManager = nullptr;

		// VMA allocation
		VulkanMemoryManager::ImageAllocation* m_ImageAllocation = nullptr;

		// Vulkan resources
		VkImageView m_ImageView = VK_NULL_HANDLE;
		VkSampler m_Sampler = VK_NULL_HANDLE;
		VkImageLayout m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;

		// Properties
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Depth = 1;
		uint32_t m_MipLevels = 1;
		uint32_t m_ArrayLayers = 1;
		TextureFormat m_Format = TextureFormat::RGBA8;
		TextureUsage m_Usage = TextureUsage::Sampled;
		bool m_GenerateMips = false;
	};
} // namespace Nightbloom