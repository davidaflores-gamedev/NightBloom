//------------------------------------------------------------------------------
// VulkanPushConstants.hpp
//
// Structs for pushing constants to Vulkan shaders.
//------------------------------------------------------------------------------

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Nightbloom
{
	// Vulkan spec guarantees 128 bytes minimum for push constants
	// Use them for per-frame and per-object data
	struct PushConstantData 
	{
	glm::mat4 model;      // 64 bytes - per object transform
	glm::mat4 view;       // 64 bytes - camera view matrix  
	glm::mat4 proj;       // 64 bytes - projection matrix
	};
	// Total: 192 bytes (still within most GPU limits of 256 bytes)

	// Smaller version if we hit limits - uses uniform buffer for view/proj
	struct CompactPushConstantData {
		glm::mat4 model;      // 64 bytes - only per-object data in push constant
	};

	// Camera matrices that rarely change (good for uniform buffer)
	struct CameraMatrices {
		glm::mat4 view;
		glm::mat4 proj;
		glm::mat4 viewProj;   // Pre-multiplied for efficiency
	};

	// For future PBR materials - but let's start simple
	struct MaterialPushConstants {
		glm::vec4 baseColor;  // 16 bytes
		float metallic;       // 4 bytes
		float roughness;      // 4 bytes
		float ao;            // 4 bytes
		float _padding;      // 4 bytes (alignment)
		// Total: 32 bytes
	};
}