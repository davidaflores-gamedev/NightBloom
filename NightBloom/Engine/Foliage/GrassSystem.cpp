//------------------------------------------------------------------------------
// GrassSystem.cpp
//------------------------------------------------------------------------------

#include "Engine/Foliage/GrassSystem.hpp"
#include "Engine/Foliage/BladeMesh.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Renderer/Frustum.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <random>
#include <cmath>
#include <algorithm>

namespace Nightbloom
{
	namespace
	{
		// Lightweight CPU value-noise (not true Perlin — a hashed lattice
		// bilinearly interpolated with a smoothstep easing curve). Good
		// enough for a placement density field; doesn't need gradient
		// continuity the way a real height/normal map would.
		float Hash2D(int x, int y, uint32_t seed)
		{
			uint32_t h = static_cast<uint32_t>(x) * 374761393u
				+ static_cast<uint32_t>(y) * 668265263u
				+ seed * 2147483647u;
			h = (h ^ (h >> 13)) * 1274126177u;
			h ^= (h >> 16);
			return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
		}

		float SmoothNoise2D(float x, float y, uint32_t seed)
		{
			int x0 = static_cast<int>(std::floor(x));
			int y0 = static_cast<int>(std::floor(y));
			float tx = x - static_cast<float>(x0);
			float ty = y - static_cast<float>(y0);
			float sx = tx * tx * (3.0f - 2.0f * tx);
			float sy = ty * ty * (3.0f - 2.0f * ty);

			float n00 = Hash2D(x0, y0, seed);
			float n10 = Hash2D(x0 + 1, y0, seed);
			float n01 = Hash2D(x0, y0 + 1, seed);
			float n11 = Hash2D(x0 + 1, y0 + 1, seed);

			float nx0 = n00 + sx * (n10 - n00);
			float nx1 = n01 + sx * (n11 - n01);
			return nx0 + sy * (nx1 - nx0);
		}

		// Multi-octave sum, normalized back to 0..1.
		float Fbm2D(float x, float y, uint32_t seed, uint32_t octaves)
		{
			float value = 0.0f;
			float amplitude = 0.5f;
			float frequency = 1.0f;
			float maxValue = 0.0f;
			for (uint32_t i = 0; i < octaves; ++i)
			{
				value += SmoothNoise2D(x * frequency, y * frequency, seed + i * 101u) * amplitude;
				maxValue += amplitude;
				amplitude *= 0.5f;
				frequency *= 2.0f;
			}
			return maxValue > 0.0f ? value / maxValue : 0.0f;
		}
	}

	bool GrassSystem::Initialize(Renderer* renderer, uint32_t maxInstanceCount)
	{
		if (!renderer)
		{
			LOG_ERROR("GrassSystem::Initialize — null renderer");
			return false;
		}

		m_Renderer = renderer;
		m_Resources = renderer->GetResourceManager();
		m_DescriptorManager = renderer->GetDescriptorManager();

		if (!m_Resources || !m_DescriptorManager)
		{
			LOG_ERROR("GrassSystem::Initialize — renderer subsystems not ready");
			return false;
		}

		m_MaxInstanceCount = maxInstanceCount;
		VkDeviceSize bufferSize = static_cast<VkDeviceSize>(maxInstanceCount) * 2ull * sizeof(glm::vec4);

		m_InstanceBuffer = m_Resources->CreateStorageBuffer("FoliageInstances", bufferSize, false);
		if (!m_InstanceBuffer)
		{
			LOG_ERROR("GrassSystem: failed to create instance storage buffer");
			return false;
		}

		m_StorageDescriptorSet = m_DescriptorManager->AllocateFoliageStorageSet();
		if (m_StorageDescriptorSet == VK_NULL_HANDLE)
		{
			LOG_ERROR("GrassSystem: failed to allocate foliage storage descriptor set");
			return false;
		}
		m_DescriptorManager->UpdateFoliageStorageSet(m_StorageDescriptorSet, m_InstanceBuffer->GetBuffer(), bufferSize);

		LOG_INFO("GrassSystem initialized (max {} instances)", maxInstanceCount);
		return true;
	}

	// =========================================================================
	// Regenerate
	// =========================================================================
	bool GrassSystem::Regenerate(const GrassDesc& desc)
	{
		if (!m_Renderer)
		{
			LOG_ERROR("GrassSystem::Regenerate — not initialized");
			return false;
		}

		m_Ready = false;
		m_Renderer->WaitForIdle();

		bool needsMesh = !m_MeshBuilt
			|| desc.segments != m_CurrentDesc.segments
			|| desc.bladeBaseHalfWidth != m_CurrentDesc.bladeBaseHalfWidth
			|| desc.taperPower != m_CurrentDesc.taperPower
			|| desc.minTipWidthFraction != m_CurrentDesc.minTipWidthFraction
			|| desc.bendAmount != m_CurrentDesc.bendAmount;

		if (needsMesh)
		{
			if (!BuildBladeMesh(desc.segments, desc.bladeBaseHalfWidth, desc.taperPower, desc.minTipWidthFraction, desc.bendAmount))
			{
				LOG_ERROR("GrassSystem::Regenerate — failed to build blade mesh");
				return false;
			}
		}

		GenerateInstances(desc);

		m_CurrentDesc = desc;
		m_Ready = true;

		LOG_INFO("GrassSystem: regenerated ({} instances, {} patches)",
			m_ActiveInstanceCount, m_Patches.size());

		return true;
	}

	// =========================================================================
	// SubmitDraw
	// =========================================================================
	void GrassSystem::SubmitDraw(DrawList& drawList, const Frustum& frustum, VkDescriptorSet terrainHeightmapSet,
		const glm::vec3& cameraPosition) const
	{
		if (!m_Ready || !m_VertexBuffer || !m_IndexBuffer || m_IndexCount == 0)
			return;

		if (terrainHeightmapSet == VK_NULL_HANDLE)
			return; // terrain not ready yet — nothing to sample height from

		// Soft slope cutoff: full grass at/above slopeThresholdDeg+falloff,
		// fully gone at/below slopeThresholdDeg-falloff, smoothstep between.
		float halfFalloffRad = glm::radians(m_CurrentDesc.slopeFalloffDeg * 0.5f);
		float thresholdRad = glm::radians(m_CurrentDesc.slopeThresholdDeg);
		float slopeThresholdCos = std::cos(thresholdRad + halfFalloffRad);
		float slopeFalloffCos = std::cos(thresholdRad - halfFalloffRad);

		// pc.model is unused as an actual transform (instance positions are
		// already world-space) — repurposed to carry terrain-bounds data, the
		// slope falloff bound, and the per-patch continuous-LOD fade params so
		// GrassSystem doesn't need to grow the shared PushConstantData struct.
		// See Grass.vert's header comment.
		//   model[0] = (terrainWorldSize, terrainHeightScale, terrainPosX, terrainPosZ)  [constant]
		//   model[1] = (slopeFalloffCos, firstInstance, drawCountF, fadeBand)            [per patch]
		glm::mat4 terrainBounds(1.0f);
		terrainBounds[0] = glm::vec4(
			m_CurrentDesc.terrainWorldSize,
			m_CurrentDesc.terrainHeightScale,
			m_CurrentDesc.terrainPosition.x,
			m_CurrentDesc.terrainPosition.z);

		glm::vec4 windParams(
			slopeThresholdCos,
			m_CurrentDesc.windStrength,
			m_CurrentDesc.windFrequency,
			m_CurrentDesc.windSpeed);

		auto smoothstepf = [](float e0, float e1, float x) -> float
		{
			float t = glm::clamp((x - e0) / glm::max(e1 - e0, 1e-4f), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		};

		for (size_t i = 0; i < m_Patches.size(); ++i)
		{
			const GrassPatch& patch = m_Patches[i];
			if (patch.fullCount == 0) continue;
			if (!frustum.Intersects(patch.center, patch.extents)) continue;

			float distance = glm::length(cameraPosition - patch.center);

			// Continuous LOD: the drawn fraction falls smoothly from 1 (within
			// lodFullDistance) to 0 (at lodFadeDistance). Beyond that the patch
			// is skipped — but its blades are already cross-faded to nothing in
			// Grass.vert, so the skip never pops.
			float drawFrac = 1.0f - smoothstepf(m_CurrentDesc.lodFullDistance, m_CurrentDesc.lodFadeDistance, distance);
			if (drawFrac <= 0.0f) continue;

			float drawCountF = drawFrac * static_cast<float>(patch.fullCount);
			uint32_t drawCount = static_cast<uint32_t>(std::ceil(drawCountF));
			if (drawCount == 0) continue;

			// Cross-fade band (a fixed blade count): the top `fadeBand` blades
			// of the drawn prefix scale-fade between 0 and 1 in Grass.vert, so
			// the marginal blade grows/shrinks smoothly as drawCountF slides
			// instead of popping on/off at an integer boundary.
			float fadeBand = glm::max(4.0f, m_CurrentDesc.lodFadeBandBlades);

			// Mesh-complexity LOD: cheap low-segment blade beyond meshLodDistance
			// (curve invisible there, far fewer per-vertex heightmap fetches).
			const bool useLow = (distance > m_CurrentDesc.meshLodDistance);

			terrainBounds[1] = glm::vec4(
				slopeFalloffCos,
				static_cast<float>(patch.firstInstance),
				drawCountF,
				fadeBand);

			DrawCommand cmd;
			cmd.pipeline = PipelineType::Foliage;
			cmd.vertexBuffer = useLow ? m_VertexBufferLow.get() : m_VertexBuffer.get();
			cmd.indexBuffer  = useLow ? m_IndexBufferLow.get()  : m_IndexBuffer.get();
			cmd.indexCount   = useLow ? m_IndexCountLow : m_IndexCount;
			cmd.instanceCount = drawCount;
			cmd.firstInstance = patch.firstInstance;

			cmd.hasPushConstants = true;
			cmd.pushConstants.model = terrainBounds;
			cmd.pushConstants.customData = windParams;

			cmd.textureDescriptorSet = m_StorageDescriptorSet;
			cmd.heightmapDescriptorSet = terrainHeightmapSet;

			drawList.AddCommand(cmd);
		}
	}

	// =========================================================================
	// Shutdown
	// =========================================================================
	void GrassSystem::Shutdown()
	{
		if (m_Renderer)
		{
			m_Renderer->WaitForIdle();
		}

		m_VertexBuffer.reset();
		m_IndexBuffer.reset();
		m_IndexCount = 0;
		m_VertexBufferLow.reset();
		m_IndexBufferLow.reset();
		m_IndexCountLow = 0;
		m_MeshBuilt = false;
		m_Patches.clear();
		m_ActiveInstanceCount = 0;
		m_Ready = false;

		// m_InstanceBuffer is owned by ResourceManager's named buffer cache
		// (same convention as FireflySystem's m_AgentBuffer) — not freed here.
		// m_StorageDescriptorSet is reclaimed by the descriptor pool's bulk
		// reset at VulkanDescriptorManager::Cleanup().

		LOG_INFO("GrassSystem shut down");
	}

	// =========================================================================
	// Private helpers
	// =========================================================================

	bool GrassSystem::BuildBladeMesh(uint32_t segments, float baseHalfWidth, float taperPower, float minTipWidthFraction, float bendAmount)
	{
		// Cheap mesh-complexity LOD: a far/mid blade with very few segments. The
		// curve is invisible past the near tier, and each grass vertex does ~5
		// heightmap fetches in Grass.vert, so fewer verts = a real saving.
		const uint32_t kLowDetailSegments = 2;

		LOG_INFO("GrassSystem: building blade meshes (near {} segments / far {} segments, halfWidth={:.3f}, bend={:.3f})",
			segments, kLowDetailSegments, baseHalfWidth, bendAmount);

		// Upload one BladeMeshData into a fresh vertex+index buffer pair.
		auto uploadMesh = [&](const char* vbName, const char* ibName, const BladeMeshData& data,
			std::unique_ptr<VulkanBuffer>& vb, std::unique_ptr<VulkanBuffer>& ib, uint32_t& indexCountOut) -> bool
		{
			if (data.vertices.empty() || data.indices.empty())
			{
				LOG_ERROR("GrassSystem: BladeMesh::Generate returned empty data");
				return false;
			}

			VkDeviceSize vbSize = data.vertices.size() * sizeof(VertexPNT);
			vb = m_Resources->CreateVertexBufferUnique(vbName, vbSize, false);
			if (!vb || !vb->UploadData(data.vertices.data(), vbSize, 0, m_Resources->GetTransferCommandPool()))
			{
				LOG_ERROR("GrassSystem: failed to create/upload blade vertex buffer '{}'", vbName);
				vb.reset();
				return false;
			}

			VkDeviceSize ibSize = data.indices.size() * sizeof(uint32_t);
			ib = m_Resources->CreateIndexBufferUnique(ibName, ibSize, false);
			if (!ib || !ib->UploadData(data.indices.data(), ibSize, 0, m_Resources->GetTransferCommandPool()))
			{
				LOG_ERROR("GrassSystem: failed to create/upload blade index buffer '{}'", ibName);
				ib.reset();
				return false;
			}

			indexCountOut = data.indexCount();
			return true;
		};

		BladeMeshData full = BladeMesh::Generate(segments, baseHalfWidth, taperPower, minTipWidthFraction, bendAmount);
		BladeMeshData low  = BladeMesh::Generate(kLowDetailSegments, baseHalfWidth, taperPower, minTipWidthFraction, bendAmount);

		if (!uploadMesh("grass_blade_vb", "grass_blade_ib", full, m_VertexBuffer, m_IndexBuffer, m_IndexCount))
			return false;
		if (!uploadMesh("grass_blade_low_vb", "grass_blade_low_ib", low, m_VertexBufferLow, m_IndexBufferLow, m_IndexCountLow))
			return false;

		m_MeshBuilt = true;

		LOG_INFO("GrassSystem: blade meshes built (near {} verts/{} idx, far {} verts/{} idx)",
			full.vertexCount(), full.indexCount(), low.vertexCount(), low.indexCount());

		return true;
	}

	void GrassSystem::GenerateInstances(const GrassDesc& desc)
	{
		m_Patches.clear();

		std::vector<glm::vec4> instanceData;
		instanceData.reserve(static_cast<size_t>(m_MaxInstanceCount) * 2);

		std::mt19937 rng(desc.seed);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);

		const float kTwoPi = 6.2831853f;
		float halfWorld = desc.terrainWorldSize * 0.5f;
		int patchCount = std::max(1, static_cast<int>(std::ceil(desc.terrainWorldSize / desc.patchSize)));
		float actualPatchSize = desc.terrainWorldSize / static_cast<float>(patchCount);
		float spacing = std::max(desc.candidateSpacing, 0.05f);

		struct Candidate { glm::vec4 d0, d1; float priority; };
		std::vector<Candidate> patchCandidates;

		bool truncated = false;

		for (int pz = 0; pz < patchCount; ++pz)
		{
			for (int px = 0; px < patchCount; ++px)
			{
				float cellMinX = -halfWorld + px * actualPatchSize;
				float cellMinZ = -halfWorld + pz * actualPatchSize;
				float cellCenterX = cellMinX + actualPatchSize * 0.5f;
				float cellCenterZ = cellMinZ + actualPatchSize * 0.5f;

				int candidatesPerSide = std::max(1, static_cast<int>(actualPatchSize / spacing));

				patchCandidates.clear();

				for (int gz = 0; gz < candidatesPerSide; ++gz)
				{
					for (int gx = 0; gx < candidatesPerSide; ++gx)
					{
						// Jitter each candidate within its grid cell so the
						// regular spacing doesn't read as a visible grid.
						float jitterX = (unit(rng) * 2.0f - 1.0f) * spacing * 0.5f;
						float jitterZ = (unit(rng) * 2.0f - 1.0f) * spacing * 0.5f;
						float worldX = cellMinX + (gx + 0.5f) * spacing + jitterX + desc.terrainPosition.x;
						float worldZ = cellMinZ + (gz + 0.5f) * spacing + jitterZ + desc.terrainPosition.z;

						float density = Fbm2D(worldX * desc.densityFrequency, worldZ * desc.densityFrequency,
							desc.seed, desc.densityOctaves);

						if (density <= desc.densityThreshold)
							continue;

						float rotY = unit(rng) * kTwoPi;
						float heightJitterFactor = 1.0f + (unit(rng) * 2.0f - 1.0f) * desc.heightJitter;
						float scale = desc.bladeHeight * heightJitterFactor;

						float tintJitter = 1.0f + (unit(rng) * 2.0f - 1.0f) * desc.colorVariation;
						float windPhase = unit(rng) * kTwoPi;

						// Independent random priority (not spatial order) —
						// taking a prefix of candidates sorted by this gives
						// a spatially-representative random subsample for
						// the LOD tiers below, not a grid-aliased "every
						// Nth" pattern.
						float priority = unit(rng);

						Candidate c;
						c.d0 = glm::vec4(worldX, worldZ, rotY, scale);
						c.d1 = glm::vec4(tintJitter, tintJitter, tintJitter, windPhase);
						c.priority = priority;
						patchCandidates.push_back(c);
					}
				}

				if (patchCandidates.empty())
					continue;

				std::sort(patchCandidates.begin(), patchCandidates.end(),
					[](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });

				uint32_t naturalTotal = static_cast<uint32_t>(patchCandidates.size());
				uint32_t remainingBudget = m_MaxInstanceCount - static_cast<uint32_t>(instanceData.size() / 2);
				uint32_t pushCount = std::min(naturalTotal, remainingBudget);
				if (pushCount < naturalTotal)
					truncated = true;

				if (pushCount == 0)
					continue;

				uint32_t patchFirst = static_cast<uint32_t>(instanceData.size() / 2);
				for (uint32_t i = 0; i < pushCount; ++i)
				{
					instanceData.push_back(patchCandidates[i].d0);
					instanceData.push_back(patchCandidates[i].d1);
				}

				GrassPatch patch;
				patch.center = glm::vec3(
					cellCenterX + desc.terrainPosition.x,
					desc.terrainPosition.y + desc.terrainHeightScale * 0.5f,
					cellCenterZ + desc.terrainPosition.z);
				patch.extents = glm::vec3(
					actualPatchSize * 0.5f,
					desc.terrainHeightScale * 0.5f + desc.bladeHeight,
					actualPatchSize * 0.5f);
				patch.firstInstance = patchFirst;
				patch.fullCount = pushCount;
				m_Patches.push_back(patch);
			}
		}

		if (truncated)
		{
			LOG_WARN("GrassSystem: hit maxInstanceCount ({}) — increase it or reduce density",
				m_MaxInstanceCount);
		}

		m_ActiveInstanceCount = static_cast<uint32_t>(instanceData.size() / 2);

		if (!instanceData.empty() && m_InstanceBuffer)
		{
			m_InstanceBuffer->UploadData(
				instanceData.data(),
				instanceData.size() * sizeof(glm::vec4),
				0,
				m_Resources->GetTransferCommandPool());
		}
	}

} // namespace Nightbloom
