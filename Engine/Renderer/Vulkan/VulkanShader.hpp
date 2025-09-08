//------------------------------------------------------------------------------
// VulkanShader.hpp
//
// Simple shader wrapper - we'll keep this minimal for now
//------------------------------------------------------------------------------

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include "Engine/Renderer/PipelineInterface.hpp"  // For ShaderStage enum

namespace Nightbloom
{
	// Forward declaration
	class VulkanDevice;

	class VulkanShader
	{
	public:
		// Constructor takes device and stage
		VulkanShader(VulkanDevice* device, ShaderStage stage);

		// Destructor cleans up
		~VulkanShader();

		// Create from SPIR-V bytecode
		bool CreateFromSpirV(const std::vector<char>& spirvCode, const std::string& entryPoint = "main");

		// Getters
		VkShaderModule GetModule() const { return m_ShaderModule; }
		ShaderStage GetStage() const { return m_Stage; }
		const std::string& GetEntryPoint() const { return m_EntryPoint; }

		// Get the shader stage info for pipeline creation
		VkPipelineShaderStageCreateInfo GetStageInfo() const;

	private:
		VulkanDevice* m_Device = nullptr;
		VkShaderModule m_ShaderModule = VK_NULL_HANDLE;
		ShaderStage m_Stage;
		std::string m_EntryPoint = "main";
	};
}