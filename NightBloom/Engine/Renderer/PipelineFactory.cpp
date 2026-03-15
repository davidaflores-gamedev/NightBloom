//------------------------------------------------------------------------------
// PipelineFactory.cpp
//
// Factory implementation for creating backend-specific pipeline managers
//------------------------------------------------------------------------------

#include "Engine/Renderer/PipelineInterface.hpp"
#include "Engine/Renderer/Vulkan/VulkanPipelineAdapter.hpp"

namespace Nightbloom
{
	// This would typically check which backend to use
	// For now, it always creates Vulkan
	std::unique_ptr<IPipelineManager> CreatePipelineManager()
	{
		// In the future, this could check a config or runtime flag
		// to determine which backend to use (Vulkan, DX12, etc.)

		return std::make_unique<VulkanPipelineAdapter>();
	}

} // namespace Nightbloom