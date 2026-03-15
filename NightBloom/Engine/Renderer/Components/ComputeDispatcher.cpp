//------------------------------------------------------------------------------
// ComputeDispatcher.cpp
//
// Implementation of compute dispatch and synchronization helpers
//------------------------------------------------------------------------------

#include "ComputeDispatcher.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanPipeline.hpp"
#include "Engine/Core/Logger/Logger.hpp"

namespace Nightbloom
{
    ComputeDispatcher::ComputeDispatcher()
    {
    }

    ComputeDispatcher::~ComputeDispatcher()
    {
        Cleanup();
    }

    bool ComputeDispatcher::Initialize(VulkanDevice* device, VulkanPipelineManager* pipelineManager)
    {
		if (!device || !pipelineManager)
		{
			LOG_ERROR("ComputeDispatcher: Invalid device or pipeline manager");
			return false;
		}

		m_Device = device;
		m_PipelineManager = pipelineManager;

		LOG_INFO("ComputeDispatcher initialized");
		return true;
    }

    void ComputeDispatcher::Cleanup()
    {
        m_Device = nullptr;
        m_PipelineManager = nullptr;
        LOG_INFO("ComputeDispatcher cleaned up");
    }

	// =========================================================================
	// Pipeline Binding
	// =========================================================================

	void ComputeDispatcher::BindPipeline(VkCommandBuffer cmd, PipelineType type)
	{
		if (!m_PipelineManager)
		{
			LOG_ERROR("ComputeDispatcher: Pipeline manager not initialized");
			return;
		}

		VkPipeline pipeline = m_PipelineManager->GetPipeline(type);
		if (pipeline == VK_NULL_HANDLE)
		{
			LOG_ERROR("ComputeDispatcher: Invalid pipeline type");
			return;
		}

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	}

	void ComputeDispatcher::BindPipeline(VkCommandBuffer cmd, VkPipeline pipeline)
	{
		if (pipeline == VK_NULL_HANDLE)
		{
			LOG_ERROR("ComputeDispatcher: Cannot bind null pipeline");
			return;
		}

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	}

	// =========================================================================
	// Descriptor Set Binding
	// =========================================================================

    void ComputeDispatcher::BindDescriptorSet(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t setIndex, VkDescriptorSet set)
    {
		if (layout == VK_NULL_HANDLE || set == VK_NULL_HANDLE)
		{
			LOG_ERROR("ComputeDispatcher: Invalid layout or descriptor set");
			return;
		}

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
			layout, setIndex, 1, &set, 0, nullptr);
    }

    void ComputeDispatcher::BindDescriptorSets(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t firstSet, const std::vector<VkDescriptorSet>& sets)
    {
		if (layout == VK_NULL_HANDLE || sets.empty())
		{
			LOG_ERROR("ComputeDispatcher: Invalid layout or empty descriptor sets");
			return;
		}

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
			layout, firstSet,
			static_cast<uint32_t>(sets.size()),
			sets.data(), 0, nullptr);
    }


	// =========================================================================
	// Push Constants
	// =========================================================================

    void ComputeDispatcher::PushConstants(VkCommandBuffer cmd, VkPipelineLayout layout, const void* data, uint32_t size, uint32_t offset)
    {
		if (layout == VK_NULL_HANDLE || !data || size == 0)
		{
			LOG_ERROR("ComputeDispatcher: Invalid push constant parameters");
			return;
		}

		vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, offset, size, data);
    }

	// =========================================================================
	// Dispatch
	// =========================================================================

    void ComputeDispatcher::Dispatch(VkCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
		if (groupCountX == 0 || groupCountY == 0 || groupCountZ == 0)
		{
			LOG_WARN("ComputeDispatcher: Zero dispatch group count");
			return;
		}

		vkCmdDispatch(cmd, groupCountX, groupCountY, groupCountZ);
    }

    void ComputeDispatcher::DispatchIndirect(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset)
    {
		if (buffer == VK_NULL_HANDLE)
		{
			LOG_ERROR("ComputeDispatcher: Invalid indirect buffer");
			return;
		}

		vkCmdDispatchIndirect(cmd, buffer, offset);
    }

    uint32_t ComputeDispatcher::CalculateGroupCount(uint32_t totalSize, uint32_t localSize)
    {
		// Round up division: (totalSize + localSize - 1) / localSize
		return (totalSize + localSize - 1) / localSize;
    }

	// =========================================================================
	// Buffer Memory Barriers
	// =========================================================================

    void ComputeDispatcher::ComputeToVertexShaderBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size)
    {
		InsertBufferBarrier(cmd, buffer, size,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT);
    }

    void ComputeDispatcher::ComputeToVertexInputBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size)
    {
		InsertBufferBarrier(cmd, buffer, size,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
    }

    void ComputeDispatcher::ComputeToFragmentBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size)
    {
		InsertBufferBarrier(cmd, buffer, size,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT);
    }

    void ComputeDispatcher::ComputeToComputeBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size)
    {
		InsertBufferBarrier(cmd, buffer, size,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT);
    }

    void ComputeDispatcher::ComputeToTransferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size)
    {
		InsertBufferBarrier(cmd, buffer, size,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT);
    }

    void ComputeDispatcher::ComputeToIndirectBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size)
    {
		InsertBufferBarrier(cmd, buffer, size,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
    }

	// =========================================================================
	// Image Memory Barriers
	// =========================================================================

    void ComputeDispatcher::ComputeWriteToGraphicsSampleBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspectMask)
    {
		InsertImageBarrier(cmd, image,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			aspectMask);
    }

    void ComputeDispatcher::ComputeWriteToFragmentSampleBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspectMask)
    {
		InsertImageBarrier(cmd, image,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			aspectMask);
    }

    void ComputeDispatcher::ComputeToComputeImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspectMask)
    {
		// Image stays in GENERAL layout for continued compute access
		InsertImageBarrier(cmd, image,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			aspectMask);
    }

    void ComputeDispatcher::TransitionImageForComputeWrite(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageAspectFlags aspectMask)
    {
		// Determine source stage and access based on old layout
		VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkAccessFlags srcAccess = 0;

		if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			srcAccess = VK_ACCESS_SHADER_READ_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			srcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
		}

		InsertImageBarrier(cmd, image,
			oldLayout,
			VK_IMAGE_LAYOUT_GENERAL,
			srcStage,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			srcAccess,
			VK_ACCESS_SHADER_WRITE_BIT,
			aspectMask);
    }

    void ComputeDispatcher::TransitionImageFromComputeWrite(VkCommandBuffer cmd, VkImage image, VkImageLayout newLayout, VkImageAspectFlags aspectMask)
    {
		// Determine destination stage and access based on new layout
		VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		VkAccessFlags dstAccess = 0;

		if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dstAccess = VK_ACCESS_SHADER_READ_BIT;
		}
		else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstAccess = VK_ACCESS_TRANSFER_READ_BIT;
		}

		InsertImageBarrier(cmd, image,
			VK_IMAGE_LAYOUT_GENERAL,
			newLayout,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			dstStage,
			VK_ACCESS_SHADER_WRITE_BIT,
			dstAccess,
			aspectMask);
    }


	// =========================================================================
	// Global Memory Barriers
	// =========================================================================

    void ComputeDispatcher::ComputeToGraphicsGlobalBarrier(VkCommandBuffer cmd)
    {
		VkMemoryBarrier memoryBarrier{};
		memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
			VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
			VK_ACCESS_INDEX_READ_BIT |
			VK_ACCESS_UNIFORM_READ_BIT;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			1, &memoryBarrier,
			0, nullptr,
			0, nullptr);
    }

    void ComputeDispatcher::ComputeToComputeGlobalBarrier(VkCommandBuffer cmd)
    {
		VkMemoryBarrier memoryBarrier{};
		memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			1, &memoryBarrier,
			0, nullptr,
			0, nullptr);
    }

	// =========================================================================
	// Private Helpers
	// =========================================================================

    void ComputeDispatcher::InsertBufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkAccessFlags srcAccess, VkAccessFlags dstAccess)
    {
		VkBufferMemoryBarrier bufferBarrier{};
		bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufferBarrier.srcAccessMask = srcAccess;
		bufferBarrier.dstAccessMask = dstAccess;
		bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarrier.buffer = buffer;
		bufferBarrier.offset = 0;
		bufferBarrier.size = size;

		vkCmdPipelineBarrier(cmd,
			srcStage,
			dstStage,
			0,
			0, nullptr,
			1, &bufferBarrier,
			0, nullptr);
    }

    void ComputeDispatcher::InsertImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkImageAspectFlags aspectMask)
    {
		VkImageMemoryBarrier imageBarrier{};
		imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageBarrier.srcAccessMask = srcAccess;
		imageBarrier.dstAccessMask = dstAccess;
		imageBarrier.oldLayout = oldLayout;
		imageBarrier.newLayout = newLayout;
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.image = image;
		imageBarrier.subresourceRange.aspectMask = aspectMask;
		imageBarrier.subresourceRange.baseMipLevel = 0;
		imageBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

		vkCmdPipelineBarrier(cmd,
			srcStage,
			dstStage,
			0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier);
    }
}
