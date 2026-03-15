// Vertex.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>

namespace Nightbloom
{
	// Original format for simple colored geometry
	struct VertexPCU
	{
		glm::vec3 position;
		glm::vec3 color;
		glm::vec2 uv;

		// Tell Vulkan how to read this vertex data from a buffer
		static VkVertexInputBindingDescription GetBindingDescription()
		{
			VkVertexInputBindingDescription bindingDesc{};
			bindingDesc.binding = 0;  // Index of the binding in the array of bindings
			bindingDesc.stride = sizeof(VertexPCU);
			bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // Move to next data entry after each vertex

			return bindingDesc;
		}

		// Tell Vulkan how to extract vertex attributes (position, color, uv)
		static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions()
		{
			std::array<VkVertexInputAttributeDescription, 3> attributeDesc{};

			// Position attribute
			attributeDesc[0].binding = 0;  // Which binding this comes from
			attributeDesc[0].location = 0;  // Location in shader (layout(location = 0))
			attributeDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;  // vec3
			attributeDesc[0].offset = offsetof(VertexPCU, position);

			// Color attribute  
			attributeDesc[1].binding = 0;
			attributeDesc[1].location = 1;  // layout(location = 1)
			attributeDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;  // vec3
			attributeDesc[1].offset = offsetof(VertexPCU, color);

			// UV attribute
			attributeDesc[2].binding = 0;
			attributeDesc[2].location = 2;  // layout(location = 2)
			attributeDesc[2].format = VK_FORMAT_R32G32_SFLOAT;  // vec2
			attributeDesc[2].offset = offsetof(VertexPCU, uv);

			return attributeDesc;
		}
	};

	// New format for rendering models with normals texcoords 
	struct VertexPNT
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 texCoord;

		static VkVertexInputBindingDescription GetBindingDescription()
		{
			VkVertexInputBindingDescription bindingDesc{};
			bindingDesc.binding = 0;
			bindingDesc.stride = sizeof(VertexPNT);
			bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			return bindingDesc;
		}

		static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions()
		{
			std::array<VkVertexInputAttributeDescription, 3> attributeDesc{};

			// Position at location 0
			attributeDesc[0].binding = 0;
			attributeDesc[0].location = 0;
			attributeDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDesc[0].offset = offsetof(VertexPNT, position);

			// Normal at location 1
			attributeDesc[1].binding = 0;
			attributeDesc[1].location = 1;
			attributeDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDesc[1].offset = offsetof(VertexPNT, normal);

			// TexCoord at location 2
			attributeDesc[2].binding = 0;
			attributeDesc[2].location = 2;
			attributeDesc[2].format = VK_FORMAT_R32G32_SFLOAT;
			attributeDesc[2].offset = offsetof(VertexPNT, texCoord);

			return attributeDesc;
		}
	};

	// Full PBR vertex format (for future use)
	struct VertexPNTT
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 texCoord;
		glm::vec4 tangent;  // w component stores handedness

		static VkVertexInputBindingDescription GetBindingDescription()
		{
			VkVertexInputBindingDescription bindingDesc{};
			bindingDesc.binding = 0;
			bindingDesc.stride = sizeof(VertexPNTT);
			bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			return bindingDesc;
		}

		static std::array<VkVertexInputAttributeDescription, 4> GetAttributeDescriptions()
		{
			std::array<VkVertexInputAttributeDescription, 4> attributeDesc{};

			attributeDesc[0].binding = 0;
			attributeDesc[0].location = 0;
			attributeDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDesc[0].offset = offsetof(VertexPNTT, position);

			attributeDesc[1].binding = 0;
			attributeDesc[1].location = 1;
			attributeDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDesc[1].offset = offsetof(VertexPNTT, normal);

			attributeDesc[2].binding = 0;
			attributeDesc[2].location = 2;
			attributeDesc[2].format = VK_FORMAT_R32G32_SFLOAT;
			attributeDesc[2].offset = offsetof(VertexPNTT, texCoord);

			attributeDesc[3].binding = 0;
			attributeDesc[3].location = 3;
			attributeDesc[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attributeDesc[3].offset = offsetof(VertexPNTT, tangent);

			return attributeDesc;
		}
	};
}