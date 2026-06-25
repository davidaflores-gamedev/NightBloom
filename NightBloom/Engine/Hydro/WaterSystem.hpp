//------------------------------------------------------------------------------
// WaterSystem.hpp
//
// A single horizontal reflective water plane. Renderer-driven for its
// reflection pass (the Renderer mirrors the camera across this plane's Y and
// re-renders the scene into a reflection target), but the plane geometry +
// draw submission are owned here, the same way TerrainSystem owns the terrain.
//
// Usage:
//   WaterSystem water;
//   water.Initialize(renderer);
//   renderer->SetWaterSystem(&water);   // so the reflection pass knows waterY
//   water.Regenerate(desc);             // when size/position changes
//   water.SubmitDraw(drawList);         // each frame
//   water.Shutdown();                   // before Renderer::Shutdown
//
// v1 scope: planar reflection + Fresnel + sun specular + cheap animated
// surface normals (no vertex displacement). Deep/shallow colors are currently
// shader-side defaults; wave/Fresnel/alpha tunables ride in the push constant.
// Refraction/depth-color and tunable colors are deferred follow-ups.
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/DrawCommandSystem.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace Nightbloom
{
	class Renderer;
	class ResourceManager;

	struct WaterDesc
	{
		// Grid (a flat plane — waves are shader-side, so low resolution is fine)
		uint32_t  resolution = 64;
		float     worldSize  = 200.0f;

		// Placement — world-space Y of the surface; position.xz centres the plane
		float     waterY     = 8.0f;  // pools in the terrain's low areas by default
		glm::vec3 position   = glm::vec3(0.0f);

		// Surface tunables (packed into the draw push constant — see SubmitDraw)
		float waveAmplitude = 0.04f;   // normal-perturbation strength (not vertex height)
		float waveSpeed     = 0.6f;    // scroll speed of the animated normals
		float fresnelPower   = 5.0f;   // Schlick exponent: higher = reflective only at grazing angles
		float alpha          = 0.85f;  // surface opacity
	};

	class WaterSystem
	{
	public:
		WaterSystem() = default;
		~WaterSystem() = default;

		WaterSystem(const WaterSystem&) = delete;
		WaterSystem& operator=(const WaterSystem&) = delete;

		// Initialize — call once after the Renderer is ready.
		bool Initialize(Renderer* renderer);

		// Regenerate — rebuilds the plane mesh if resolution/worldSize changed.
		// Waits for device idle (same convention as TerrainSystem::Regenerate).
		bool Regenerate(const WaterDesc& desc);

		// SubmitDraw — adds the water draw command to the frame draw list.
		void SubmitDraw(DrawList& drawList) const;

		// Shutdown — must be called before Renderer shuts down.
		void Shutdown();

		bool IsReady() const { return m_Ready; }

		// The Renderer reads this to build the mirror matrix for the reflection
		// pass (reflect the camera across plane y = GetWaterY()).
		float GetWaterY() const { return m_CurrentDesc.waterY; }

		WaterDesc& GetDesc() { return m_CurrentDesc; }
		const WaterDesc& GetDesc() const { return m_CurrentDesc; }

	private:
		bool BuildPlaneMesh(uint32_t resolution, float worldSize);

		Renderer*        m_Renderer = nullptr;
		ResourceManager* m_Resources = nullptr;

		std::unique_ptr<VulkanBuffer> m_VertexBuffer;
		std::unique_ptr<VulkanBuffer> m_IndexBuffer;
		uint32_t                      m_IndexCount = 0;

		WaterDesc m_CurrentDesc;
		bool      m_Ready = false;
		bool      m_MeshBuilt = false;
		mutable bool m_SkipNextDraw = false;
	};

} // namespace Nightbloom
