//------------------------------------------------------------------------------
// VulkanShader.cpp
//
// Simple shader implementation
//------------------------------------------------------------------------------

#include "Engine/Renderer/Vulkan/VulkanShader.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Core/Logger/Logger.hpp"

namespace Nightbloom
{
	VulkanShader::VulkanShader(VulkanDevice* device, ShaderStage stage)
		: m_Device(device)
		, m_Stage(stage)
	{
		LOG_TRACE("Creating VulkanShader for stage: {}", static_cast<int>(stage));
	}

	VulkanShader::~VulkanShader()
	{
		if (m_ShaderModule != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(m_Device->GetDevice(), m_ShaderModule, nullptr);
			LOG_TRACE("Destroyed shader module");
		}
	}

	bool VulkanShader::CreateFromSpirV(const std::vector<char>& spirvCode, const std::string& entryPoint)
	{
		if (spirvCode.empty())
		{
			LOG_ERROR("Empty SPIR-V code provided");
			return false;
		}

		m_EntryPoint = entryPoint;

		// Create shader module
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = spirvCode.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(spirvCode.data());

		if (vkCreateShaderModule(m_Device->GetDevice(), &createInfo, nullptr, &m_ShaderModule) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create shader module");
			return false;
		}

		LOG_INFO("Created shader module successfully");
		return true;
	}

	VkPipelineShaderStageCreateInfo VulkanShader::GetStageInfo() const
	{
		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

		// Convert our ShaderStage enum to Vulkan's enum
		switch (m_Stage)
		{
		case ShaderStage::Vertex:
			stageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			break;
		case ShaderStage::Fragment:
			stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			break;
		case ShaderStage::Geometry:
			stageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
			break;
		case ShaderStage::Compute:
			stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			break;
		default:
			LOG_ERROR("Unknown shader stage");
			stageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		}

		stageInfo.module = m_ShaderModule;
		stageInfo.pName = m_EntryPoint.c_str();
		stageInfo.pSpecializationInfo = nullptr;

		return stageInfo;
	}
}