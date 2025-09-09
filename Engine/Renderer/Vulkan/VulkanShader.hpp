//------------------------------------------------------------------------------
// VulkanShader.hpp
//
// Simple shader wrapper - we'll keep this minimal for now
//------------------------------------------------------------------------------

#pragma once

#include <vulkan/vulkan.h>
#include "Engine/Renderer/RenderDevice.hpp"

namespace Nightbloom
{
	// Forward declaration
	class VulkanDevice;

	class VulkanShader : Shader
	{
	public:
		// Constructor takes device and stage
		VulkanShader(VulkanDevice* device, ShaderStage stage);

		// Destructor cleans up
		~VulkanShader() override;

		// Create from SPIR-V bytecode

		// Getters
		ShaderStage GetStage() const override { return m_Stage; }
		const std::string& GetEntryPoint() const override { return m_EntryPoint; }
		const std::string& GetSourcePath() const override { return m_SourcePath; }


		bool CreateFromSpirV(const std::vector<char>& spirvCode, const std::string& entryPoint = "main");
		VkShaderModule GetModule() const { return m_ShaderModule; }
		VkPipelineShaderStageCreateInfo GetStageInfo() const;
		void SetSourcePath(const std::string& path) { m_SourcePath = path; }

	private:
		VulkanDevice* m_Device = nullptr;
		VkShaderModule m_ShaderModule = VK_NULL_HANDLE;
		ShaderStage m_Stage;
		std::string m_EntryPoint = "main";
		std::string m_SourcePath;
	};
}