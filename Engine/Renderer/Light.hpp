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
#include <cstdint>
#include <string>

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
	// Scene lighting UBO - uploaded once per frame
	//
	// This maps directly to a GLSL uniform block:
	//
	//   layout(std140, set = 2, binding = 0) uniform SceneLighting {
	//       LightData lights[16];
	//       vec4 ambient;
	//       int  numLights;
	//   };
	//
	// Total size: 16 * 48 + 16 + 16 = 800 bytes
	// (numLights is in its own 16-byte aligned slot due to std140)
	//--------------------------------------------------------------------------

	static constexpr uint32_t MAX_LIGHTS = 16;

	struct SceneLightingData
	{
		LightData lights[MAX_LIGHTS];
		glm::vec4 ambient = glm::vec4(0.03f, 0.03f, 0.05f, 1.0f);  // rgb = color, a = intensity
		int       numLights = 0;
		float     _padding[3] = { 0.0f, 0.0f, 0.0f };              // pad to 16-byte alignment
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

		//Shared
		glm::vec3 color = glm::vec3(1.0f);
		float intensity = 1.0f;

		// Directional: direction the light is shining (normalized)
		glm::vec3 direction = glm::vec3(0.0f, 5.0f, 0.0f);

		// Point: world position + falloff
		glm::vec3 position = glm::vec3(0.0f, 5.0f, 0.0f);
		float constant = 1.0f;
		float linear = 0.09f;
		float quadratic = 0.032f;
		float radius = 50.0f;

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
	};
} // namespace Nightbloom
