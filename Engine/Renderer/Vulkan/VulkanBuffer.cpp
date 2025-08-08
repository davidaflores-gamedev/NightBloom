#include "VulkanBuffer.hpp"
#include "VulkanDevice.hpp"
#include "VulkanCommandPool.hpp"
#include "Core/Logger/Logger.hpp"

namespace Nightbloom
{
	Nightbloom::VulkanBuffer::VulkanBuffer(VulkanDevice* device)
		: m_Device(device)
	{
	}

	Nightbloom::VulkanBuffer::~VulkanBuffer()
	{
		Destroy();
	}

	bool Nightbloom::VulkanBuffer::Create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
	{
		m_Size = size;

		// Create buffer
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(m_Device->GetDevice(), &bufferInfo, nullptr, &m_Buffer) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create Vulkan buffer");
			return false;
		}

		// Get memory requirements
		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(m_Device->GetDevice(), m_Buffer, &memRequirements);

		// Allocate memory
		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

		// THIS WAS MISSING! Allocate the memory
		if (vkAllocateMemory(m_Device->GetDevice(), &allocInfo, nullptr, &m_Memory) != VK_SUCCESS) {
			LOG_ERROR("Failed to allocate buffer memory");
			vkDestroyBuffer(m_Device->GetDevice(), m_Buffer, nullptr);  // Clean up buffer
			m_Buffer = VK_NULL_HANDLE;
			return false;
		}

		// Bind buffer to memory
		if (vkBindBufferMemory(m_Device->GetDevice(), m_Buffer, m_Memory, 0) != VK_SUCCESS) {
			LOG_ERROR("Failed to bind buffer memory");
			vkFreeMemory(m_Device->GetDevice(), m_Memory, nullptr);
			vkDestroyBuffer(m_Device->GetDevice(), m_Buffer, nullptr);
			m_Buffer = VK_NULL_HANDLE;
			m_Memory = VK_NULL_HANDLE;
			return false;
		}

		// If host visible , map it persistently
		if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
			if (vkMapMemory(m_Device->GetDevice(), m_Memory, 0, size, 0, &m_MappedMemory))
			{
				LOG_ERROR("Failed to map buffer memory");
				m_MappedMemory = nullptr;
			}
		}

		return true;

	}

	bool Nightbloom::VulkanBuffer::CopyData(const void* data, VkDeviceSize size)
	{
		if (!m_MappedMemory || size > m_Size)
		{
			LOG_ERROR("Invalid buffer mapping or size");
			return false;
		}

		memcpy(m_MappedMemory, data, static_cast<size_t>(size));

		// Flush if not coherent
		VkMappedMemoryRange range = {};
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.memory = m_Memory;
		range.offset = 0;
		range.size = size;
		vkFlushMappedMemoryRanges(m_Device->GetDevice(), 1, &range);

		return true;
	}

	bool Nightbloom::VulkanBuffer::CopyFrom(VulkanBuffer& srcBuffer, VkDeviceSize size, VulkanCommandPool* commandPool)
	{
		// Create one-time command buffer

		m_Device;
		VulkanSingleTimeCommand cmd(m_Device, commandPool);
		VkCommandBuffer commandBuffer = cmd.Begin();

		VkBufferCopy copyRegion = {};
		copyRegion.srcOffset = 0; // Optional
		copyRegion.dstOffset = 0; // Optional
		copyRegion.size = size;
		vkCmdCopyBuffer(commandBuffer, srcBuffer.GetBuffer(), m_Buffer, 1, &copyRegion);

		cmd.End();
		return true;
	}

	void Nightbloom::VulkanBuffer::Destroy()
	{
		if (m_MappedMemory)
		{
			vkUnmapMemory(m_Device->GetDevice(), m_Memory);
			m_MappedMemory = nullptr;
		}

		if (m_Buffer != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_Device->GetDevice(), m_Buffer, nullptr);
			m_Buffer = VK_NULL_HANDLE;
		}

		if (m_Memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_Device->GetDevice(), m_Memory, nullptr);
			m_Memory = VK_NULL_HANDLE;
		}

		m_Size = 0;
	}

	uint32_t Nightbloom::VulkanBuffer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
	{
		VkPhysicalDeviceMemoryProperties memProps;
		vkGetPhysicalDeviceMemoryProperties(m_Device->GetPhysicalDevice(), &memProps);

		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}

		LOG_ERROR("Failed to find suitable memory type");
		return UINT32_MAX; // Indicate failure
	}
}
