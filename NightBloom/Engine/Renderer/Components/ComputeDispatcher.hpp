//------------------------------------------------------------------------------
// ComputeDispatcher.hpp
//
// High-level helper for compute shader dispatch and synchronization
//------------------------------------------------------------------------------
#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace Nightbloom
{
	//Forward declarations
	class VulkanDevice;
	class VulkanPipelineManager;
	enum class PipelineType;

	class ComputeDispatcher
	{
	public:
		ComputeDispatcher();
		~ComputeDispatcher();

		bool Initialize(VulkanDevice* device, VulkanPipelineManager* pipelineManager);
		void Cleanup();

		// =====================================================================
		// Pipeline Binding
		// =====================================================================

		void BindPipeline(VkCommandBuffer cmd, PipelineType type);
		void BindPipeline(VkCommandBuffer cmd, VkPipeline pipeline);


		// =====================================================================
		// Descriptor Set Binding
		// =====================================================================

		void BindDescriptorSet(VkCommandBuffer cmd, VkPipelineLayout layout,
			uint32_t setIndex, VkDescriptorSet set);
		void BindDescriptorSets(VkCommandBuffer cmd, VkPipelineLayout layout,
			uint32_t firstSet,
			const std::vector<VkDescriptorSet>& sets);

		// =====================================================================
		// Push Constants
		// =====================================================================

		void PushConstants(VkCommandBuffer cmd, VkPipelineLayout layout,
			const void* data, uint32_t size, uint32_t offset = 0);

		// =====================================================================
		// Dispatch
		// =====================================================================

		void Dispatch(VkCommandBuffer cmd,
			uint32_t groupCountX,
			uint32_t groupCountY = 1,
			uint32_t groupCountZ = 1);

		void DispatchIndirect(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset = 0);

		// Helper to calculate dispatch group count from total size and local workgroup size
		static uint32_t CalculateGroupCount(uint32_t totalSize, uint32_t localSize);

		// =====================================================================
		// Buffer Memory Barriers
		// =====================================================================

		// After compute writes, before vertex shader reads (SSBO in vertex shader)
		void ComputeToVertexShaderBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size);

		// After compute writes, before vertex input reads (compute output as vertex buffer)
		void ComputeToVertexInputBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size);

		// After compute writes, before fragment shader reads
		void ComputeToFragmentBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size);

		// After compute writes, before next compute pass reads
		void ComputeToComputeBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size);

		// After compute writes, before transfer/readback
		void ComputeToTransferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size);

		// After compute writes, before indirect draw/dispatch reads
		void ComputeToIndirectBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size);

		// =====================================================================
		// Image Memory Barriers (for 3D textures, storage images, etc.)
		// =====================================================================

		// After compute writes image, before graphics pipeline samples it
		void ComputeWriteToGraphicsSampleBarrier(VkCommandBuffer cmd, VkImage image,
			VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

		// After compute writes image, before fragment shader samples it
		void ComputeWriteToFragmentSampleBarrier(VkCommandBuffer cmd, VkImage image,
			VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

		// Between compute passes that read/write the same image
		void ComputeToComputeImageBarrier(VkCommandBuffer cmd, VkImage image,
			VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

		// Transition image layout with compute shader access
		void TransitionImageForComputeWrite(VkCommandBuffer cmd, VkImage image,
			VkImageLayout oldLayout,
			VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

		void TransitionImageFromComputeWrite(VkCommandBuffer cmd, VkImage image,
			VkImageLayout newLayout,
			VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

		// =====================================================================
		// Global Memory Barriers (when specific resources aren't tracked)
		// =====================================================================

		// Full barrier between compute and all graphics stages
		void ComputeToGraphicsGlobalBarrier(VkCommandBuffer cmd);

		// Barrier between compute passes
		void ComputeToComputeGlobalBarrier(VkCommandBuffer cmd);

		private:
			VulkanDevice* m_Device = nullptr;
			VulkanPipelineManager* m_PipelineManager = nullptr;

			// Helper for buffer memory barriers
			void InsertBufferBarrier(VkCommandBuffer cmd,
				VkBuffer buffer,
				VkDeviceSize size,
				VkPipelineStageFlags srcStage,
				VkPipelineStageFlags dstStage,
				VkAccessFlags srcAccess,
				VkAccessFlags dstAccess);

			// Helper for image memory barriers
			void InsertImageBarrier(VkCommandBuffer cmd,
				VkImage image,
				VkImageLayout oldLayout,
				VkImageLayout newLayout,
				VkPipelineStageFlags srcStage,
				VkPipelineStageFlags dstStage,
				VkAccessFlags srcAccess,
				VkAccessFlags dstAccess,
				VkImageAspectFlags aspectMask);
	};
}