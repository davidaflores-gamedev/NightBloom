//------------------------------------------------------------------------------
// Light.hpp
//
// Light data structures for scene lighting
// API-agnostic - these are pure data structs used by both CPU and GPU
//
// GPU Layout (std140):
//   Set 2, Binding 0 = SceneLightingData UBO
//------------------------------------------------------------------------------
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <string>
#include <cmath>

namespace Nightbloom
{
	//--------------------------------------------------------------------------
	// Light types
	//--------------------------------------------------------------------------
	enum class LightType : int
	{
		Directional = 0,
		Point = 1
		// Spot = 2  // Future
	};

	//--------------------------------------------------------------------------
	// Single light - packed for std140 alignment (all vec4s)
	//
	// Directional: position.xyz = light direction (pointing FROM the light)
	//              attenuation is unused
	//
	// Point:       position.xyz = world position
	//              attenuation.xyz = constant/linear/quadratic falloff
	//              attenuation.w   = max radius (for culling / shader cutoff)
	//--------------------------------------------------------------------------

	struct LightData
	{
		glm::vec4 position = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);  // xyz = pos/dir, w = type (0=dir, 1=point)
		glm::vec4 color = glm::vec4(1.f); // rgb = color, a = intensity
		glm::vec4 attenuation = glm::vec4(1.0f, 0.09f, 0.032f, 50.0f);// constant, linear, quadratic, radius
	};

	//--------------------------------------------------------------------------
	// Shadow data - GPU struct for shadow mapping
	// Must match GLSL ShadowData struct exactly!
	//--------------------------------------------------------------------------
	struct ShadowData
	{
		glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);  // View-projection from light's perspective
		glm::vec4 shadowParams = glm::vec4(0.005f, 0.02f, 0.0f, 1.0f);
		// x = bias, y = normalBias, z = unused, w = enabled (1.0 = enabled, 0.0 = disabled)
	};

	//--------------------------------------------------------------------------
	// Scene lighting UBO - uploaded once per frame
	//
	// This maps directly to a GLSL uniform block:
	//
	//   layout(std140, set = 2, binding = 0) uniform SceneLighting {
	//       LightData lights[16];
	//       vec4 ambient;
	//       int  numLights;
	//       float _padding[3];
	//       ShadowData shadowData;
	//   };
	//--------------------------------------------------------------------------

	static constexpr uint32_t MAX_LIGHTS = 16;

	struct SceneLightingData
	{
		LightData lights[MAX_LIGHTS];
		glm::vec4 ambient = glm::vec4(0.03f, 0.03f, 0.05f, 1.0f);  // rgb = color, a = intensity
		int       numLights = 0;
		int _pad1 = 0, _pad2 = 0, _pad3 = 0;              // pad to 16-byte alignment
		ShadowData shadowData;  // Shadow mapping data
	};

	//--------------------------------------------------------------------------
	// Shadow configuration - CPU-side settings for shadow casting
	//--------------------------------------------------------------------------
	struct ShadowConfig
	{
		bool  castsShadows = false;
		float orthoSize = 25.0f;      // Half-size of orthographic frustum
		float nearPlane = 0.1f;
		float farPlane = 100.0f;
		float bias = 0.005f;          // Depth bias to reduce shadow acne
		float normalBias = 0.02f;     // Normal-based bias
	};

	//--------------------------------------------------------------------------
	// CPU-side light helper (for Scene/Editor use)
	// Provides a friendlier interface than raw LightData
	//--------------------------------------------------------------------------
	struct Light
	{
		std::string name = "Light";
		LightType	type = LightType::Directional;
		bool		enabled = true;

		// Shared
		glm::vec3 color = glm::vec3(1.0f);
		float intensity = 1.0f;

		// Directional: direction the light is shining (normalized)
		glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);

		// Point: world position + falloff
		glm::vec3 position = glm::vec3(0.0f, 5.0f, 0.0f);
		float constant = 1.0f;
		float linear = 0.09f;
		float quadratic = 0.032f;
		float radius = 50.0f;

		// Shadow configuration
		ShadowConfig shadowConfig;

		//----------------------------------------------------------------------
		// Pack into GPU-ready LightData
		//----------------------------------------------------------------------
		LightData ToGPUData() const
		{
			LightData data;

			if (type == LightType::Directional)
			{
				// Direction stored in xyz, w = 0 signals directional
				data.position = glm::vec4(glm::normalize(direction), 0.0f);
			}
			else // Point
			{
				// Position stored in xyz, w = 1 signals point
				data.position = glm::vec4(position, 1.0f);
			}

			data.color = glm::vec4(color, intensity);
			data.attenuation = glm::vec4(constant, linear, quadratic, radius);

			return data;
		}

		//----------------------------------------------------------------------
		// Calculate light-space matrix for shadow mapping
		// center: point the shadow frustum is centered on (e.g., player position)
		//----------------------------------------------------------------------
		glm::mat4 CalculateLightSpaceMatrix(const glm::vec3& center = glm::vec3(0.0f)) const
		{
			if (type == LightType::Directional)
			{
				glm::vec3 lightDir = glm::normalize(direction);

				// Position the light "behind" the scene looking toward center
				glm::vec3 lightPos = center - lightDir * shadowConfig.farPlane * 0.5f;

				// Choose up vector that's not parallel to light direction
				glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
				if (std::abs(glm::dot(lightDir, up)) > 0.99f)
				{
					// Light is pointing nearly straight up or down
					up = glm::vec3(0.0f, 0.0f, 1.0f);
				}

				glm::mat4 lightView = glm::lookAt(lightPos, center, up);

				// Orthographic projection for directional light
				float size = shadowConfig.orthoSize;
				glm::mat4 lightProjection = glm::ortho(
					-size, size,
					-size, size,
					shadowConfig.nearPlane, shadowConfig.farPlane
				);

				return lightProjection * lightView;
			}
			else
			{
				// Point lights would use perspective + cube map (not implemented)
				return glm::mat4(1.0f);
			}
		}

		//----------------------------------------------------------------------
		// Generate GPU shadow data
		// center: point the shadow frustum is centered on
		//----------------------------------------------------------------------
		ShadowData ToShadowData(const glm::vec3& center = glm::vec3(0.0f)) const
		{
			ShadowData data;

			if (type == LightType::Directional && shadowConfig.castsShadows)
			{
				data.lightSpaceMatrix = CalculateLightSpaceMatrix(center);
				data.shadowParams = glm::vec4(
					shadowConfig.bias,
					shadowConfig.normalBias,
					0.0f,
					1.0f  // w = 1.0 means shadows ENABLED
				);
			}
			else
			{
				// Shadows disabled
				data.lightSpaceMatrix = glm::mat4(1.0f);
				data.shadowParams = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);  // w = 0.0 means DISABLED
			}

			return data;
		}
	};
} // namespace Nightbloom