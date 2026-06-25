//------------------------------------------------------------------------------
// GrassSystem.hpp
//
// Instanced grass blade rendering. A real tapered blade mesh (BladeMesh) is
// uploaded once and drawn via instancing; per-instance world position,
// rotation, scale, color tint, and wind phase live in a storage buffer.
//
// Placement is a dense candidate grid (spacing = candidateSpacing) scattered
// across the terrain's world bounds, where each candidate is kept or
// dropped based on a multi-octave CPU noise sample (see GrassSystem.cpp's
// Fbm2D) compared against densityThreshold — the same "sample a noise
// field to decide what's here" trick the heightmap itself uses, just
// CPU-side and binary instead of a continuous height. This produces patchy,
// organic-looking clusters for free, without an explicit "clump" object —
// an earlier pass used discrete random clump centers with blades scattered
// around each one, which read as a uniform grid of separate dots rather
// than continuous-looking cover; replaced for that reason. A future
// "authored placement" pass (if ever needed) would swap the noise sample
// for a painted density mask texture — same per-candidate accept/reject
// shape, no architecture change.
//
// Height/slope are sampled from the terrain heightmap in Grass.vert — there
// is no CPU-side height query in this engine.
//
// Instances are grouped into world-space grid patches at generation time
// (sorted contiguously into the storage buffer, one firstInstance offset per
// patch) so SubmitDraw can frustum-cull whole patches against the existing
// Frustum::Intersects test and submit one DrawCommand per visible patch.
//
// Distance LOD: each patch's candidates are shuffled by an independent
// random priority (not spatial order — avoids grid/banding artifacts a
// strided "every Nth" thinning would show) before being appended, so a
// shorter prefix of the same range is a spatially-representative random
// subsample of the full set. Three nested tiers (far ⊂ mid ⊂ full) share one
// firstInstance — SubmitDraw just submits a shorter instanceCount at
// distance, same range, no second buffer or duplicate data. This actually
// reduces per-frame instance/vertex cost at range, unlike the visibility-only
// width boost in Grass.vert. Tier selection has Terrain-style hysteresis
// (TerrainSystem::SelectLODResolution) to avoid flicker at the boundary.
//
// Usage:
//   GrassSystem grass;
//   grass.Initialize(renderer);
//   grass.Regenerate(desc);                            // call whenever params change
//   grass.SubmitDraw(drawList, frustum, heightmapSet, cameraPosition); // call each frame
//   grass.Shutdown();
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/DrawCommandSystem.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Nightbloom
{
	class Renderer;
	class ResourceManager;
	class VulkanDescriptorManager;
	struct Frustum;

	struct GrassDesc
	{
		// Blade mesh shape (triggers a mesh rebuild if changed)
		uint32_t segments = 4;
		float    bladeBaseHalfWidth = 0.1f;
		float    taperPower = 1.5f;
		// Tip half-width as a fraction of bladeBaseHalfWidth — kept above 0
		// so the tip isn't a literal zero-width point (sub-pixel triangles
		// there alias/shimmer at a distance). See BladeMesh.hpp.
		float    minTipWidthFraction = 0.18f;

		// Per-instance height (world units) — actual height is this value
		// jittered by +/- heightJitter fraction, baked into each instance's
		// "scale" since the blade mesh's local height is normalized to 1.0.
		float bladeHeight = 0.6f;
		float heightJitter = 0.3f;

		// Placement — patches are grid cells of patchSize world units, used
		// only for frustum culling. Within each patch, candidates sit on a
		// regular grid of candidateSpacing world units (each jittered within
		// its cell so the grid itself isn't visible), kept or dropped by a
		// noise sample — see this struct's header comment. candidateSpacing
		// vs. bladeBaseHalfWidth*2 is the key ratio for "continuous carpet"
		// vs. "individual hairs poking out of bare dirt" — width needs to be
		// a sizeable fraction of the spacing for neighboring blades to read
		// as overlapping coverage rather than isolated spikes.
		float    patchSize = 25.0f;
		float    candidateSpacing = 0.6f;
		float    densityThreshold = 0.5f;   // candidate kept if noise > this
		float    densityFrequency = 0.06f;  // world units per noise cycle (~1/frequency)
		uint32_t densityOctaves = 2;

		// Wind (blows along a fixed world +X direction in Grass.vert — a v1
		// simplification, see that shader's header comment)
		float windSpeed = 1.0f;
		float windStrength = 0.15f;
		float windFrequency = 0.3f;

		// Cliff avoidance — blades on slopes steeper than slopeThresholdDeg
		// fade to zero scale over slopeFalloffDeg degrees in Grass.vert (a
		// soft transition, not a hard on/off cutoff — avoids a visible wall
		// of grass stopping exactly at the threshold).
		float slopeThresholdDeg = 45.0f;
		float slopeFalloffDeg = 10.0f;

		// Distance visibility — thin blades shrink below a pixel and
		// disappear at range (no real LOD exists yet). Grass.vert widens a
		// blade's local width once its apparent size (proj[1][1]/distance,
		// a cheap FOV/distance proxy, not exact device pixels) drops below
		// minApparentWidth, clamped to at most maxWidthBoost times wider.
		// A "fake it" fix, not true LOD — doesn't reduce instance/triangle
		// cost the way real distance LOD would.
		float minApparentWidth = 0.02f;
		float maxWidthBoost = 8.0f;

		// Distance LOD — real cost reduction (fewer instances actually
		// drawn), complementary to the visibility-only width boost above.
		// Patches closer than lodMidDistance draw all instances; beyond
		// lodFarDistance only lodFarFraction of them; lodMidFraction in
		// between. See this file's header comment for how the tiers share
		// one contiguous range with no duplicate data.
		float lodMidDistance = 40.0f;
		float lodFarDistance = 90.0f;
		float lodMidFraction = 0.45f;
		float lodFarFraction = 0.15f;

		// Per-instance color tint jitter, multiplicative around 1.0
		float colorVariation = 0.15f;

		uint32_t seed = 1337;

		// Terrain world bounds — needed to scatter placement and to convert
		// world XZ to heightmap UV in Grass.vert (no CPU height query exists,
		// so this just describes where the heightmap texture maps in world
		// space, the same convention TerrainSystem itself uses).
		glm::vec3 terrainPosition = glm::vec3(0.0f);
		float     terrainWorldSize = 200.0f;
		float     terrainHeightScale = 30.0f;
	};

	class GrassSystem
	{
	public:
		GrassSystem() = default;
		~GrassSystem() = default;

		GrassSystem(const GrassSystem&) = delete;
		GrassSystem& operator=(const GrassSystem&) = delete;

		//----------------------------------------------------------------------
		// Initialize — call once after Renderer is ready. Allocates the
		// instance storage buffer + descriptor set sized for maxInstanceCount
		// up front, so Regenerate() never needs to reallocate the descriptor
		// set (avoids the per-regenerate descriptor-set leak TerrainSystem's
		// heightmap path has — see CLAUDE.md/ROADMAP.md notes on that).
		//----------------------------------------------------------------------
		bool Initialize(Renderer* renderer, uint32_t maxInstanceCount = 98304);

		//----------------------------------------------------------------------
		// Regenerate — rebuilds the blade mesh only if shape params changed,
		// always re-scatters placement and re-uploads the instance buffer.
		// Waits for device idle, same convention as TerrainSystem::Regenerate.
		//----------------------------------------------------------------------
		bool Regenerate(const GrassDesc& desc);

		//----------------------------------------------------------------------
		// SubmitDraw — frustum-culls patches and adds one DrawCommand per
		// visible patch to the frame draw list, picking a distance LOD tier
		// (full/mid/far instance count) per patch. Call once per frame.
		//
		// terrainHeightmapSet is TerrainSystem's heightmap descriptor set
		// (set 4) — GrassSystem has no heightmap of its own, it samples the
		// same terrain heightmap Terrain.vert does, so the caller (GrassPanel)
		// must pass it through each frame.
		//----------------------------------------------------------------------
		void SubmitDraw(DrawList& drawList, const Frustum& frustum, VkDescriptorSet terrainHeightmapSet,
			const glm::vec3& cameraPosition) const;

		//----------------------------------------------------------------------
		// Shutdown — must be called before Renderer shuts down
		//----------------------------------------------------------------------
		void Shutdown();

		bool IsReady() const { return m_Ready; }
		uint32_t GetActiveInstanceCount() const { return m_ActiveInstanceCount; }
		uint32_t GetPatchCount() const { return static_cast<uint32_t>(m_Patches.size()); }
		const GrassDesc& GetDesc() const { return m_CurrentDesc; }

	private:
		struct GrassPatch
		{
			glm::vec3 center;
			glm::vec3 extents;
			uint32_t  firstInstance;
			// Nested tiers sharing firstInstance — see this file's header
			// comment. farCount <= midCount <= fullCount by construction.
			uint32_t  farCount;
			uint32_t  midCount;
			uint32_t  fullCount;
		};

		bool BuildBladeMesh(uint32_t segments, float baseHalfWidth, float taperPower, float minTipWidthFraction);
		void GenerateInstances(const GrassDesc& desc);
		int SelectLODTier(float distance, int currentTier) const;

		Renderer* m_Renderer = nullptr;
		ResourceManager* m_Resources = nullptr;
		VulkanDescriptorManager* m_DescriptorManager = nullptr;

		// Blade mesh — shared by every instance
		std::unique_ptr<VulkanBuffer> m_VertexBuffer;
		std::unique_ptr<VulkanBuffer> m_IndexBuffer;
		uint32_t m_IndexCount = 0;

		// Per-instance storage buffer — sized once for m_MaxInstanceCount
		VulkanBuffer* m_InstanceBuffer = nullptr; // owned by ResourceManager's named buffer cache
		VkDescriptorSet m_StorageDescriptorSet = VK_NULL_HANDLE;
		uint32_t m_MaxInstanceCount = 0;
		uint32_t m_ActiveInstanceCount = 0;

		std::vector<GrassPatch> m_Patches;
		// Per-patch last-selected LOD tier (0=full,1=mid,2=far), for the
		// hysteresis in SelectLODTier — mutated from the const SubmitDraw,
		// same convention as TerrainSystem's mutable m_SkipNextDraw.
		mutable std::vector<int> m_PatchTier;

		GrassDesc m_CurrentDesc;
		bool m_Ready = false;
		bool m_MeshBuilt = false;
	};

} // namespace Nightbloom
