#include "VulkanBuffer.hpp"

//#include "VulkanCommandPool.hpp"
//#include "Core/Logger/Logger.hpp"
//
//namespace Nightbloom
//{
//	Nightbloom::VulkanBuffer::VulkanBuffer(VulkanDevice* device)
//		: m_Device(device)
//	{
//	}
//
//	Nightbloom::VulkanBuffer::~VulkanBuffer()
//	{
//		Destroy();
//	}
//
//	bool Nightbloom::VulkanBuffer::Create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
//	{
//		m_Size = size;
//
//		// Create buffer
//		VkBufferCreateInfo bufferInfo = {};
//		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
//		bufferInfo.size = size;
//		bufferInfo.usage = usage;
//		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
//
//		if (vkCreateBuffer(m_Device->GetDevice(), &bufferInfo, nullptr, &m_Buffer) != VK_SUCCESS)
//		{
//			LOG_ERROR("Failed to create Vulkan buffer");
//			return false;
//		}
//
//		// Get memory requirements
//		VkMemoryRequirements memRequirements;
//		vkGetBufferMemoryRequirements(m_Device->GetDevice(), m_Buffer, &memRequirements);
//
//		// Allocate memory
//		VkMemoryAllocateInfo allocInfo = {};
//		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
//		allocInfo.allocationSize = memRequirements.size;
//		allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);
//
//		// Bind buffer to memory
//		vkBindBufferMemory(m_Device->GetDevice(), m_Buffer, m_Memory, 0);
//
//		// If host visible , map it persistently
//		if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
//			vkMapMemory(m_Device->GetDevice(), m_Memory, 0, size, 0, &m_MappedMemory);
//		}
//
//		return true;
//
//	}
//
//	bool Nightbloom::VulkanBuffer::CopyData(const void* data, VkDeviceSize size)
//	{
//		if (!m_MappedMemory || size > m_Size)
//		{
//			LOG_ERROR("Invalid buffer mapping or size");
//			return false;
//		}
//
//		memcpy(m_MappedMemory, data, static_cast<size_t>(size));
//
//		// Flush if not coherent
//		VkMappedMemoryRange range = {};
//		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
//		range.memory = m_Memory;
//		range.offset = 0;
//		range.size = size;
//		vkFlushMappedMemoryRanges(m_Device->GetDevice(), 1, &range);
//	}
//
//	bool Nightbloom::VulkanBuffer::CopyFrom(VulkanBuffer& srcBuffer, VkDeviceSize size, VkCommandPool* commandPool)
//	{
//		// Create one-time command buffer
//
//		m_Device;
//		VulkanSingleTimeCommand cmd(m_Device, commandPool);
//		VkCommandBuffer commandBuffer = cmd.Begin();
//	}
//
//	void Nightbloom::VulkanBuffer::Destroy()
//	{
//	}
//
//	uint32_t Nightbloom::VulkanBuffer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
//	{
//		return 0;
//	}
//}
//