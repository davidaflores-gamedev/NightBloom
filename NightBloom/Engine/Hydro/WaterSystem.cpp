//------------------------------------------------------------------------------
// WaterSystem.cpp
//------------------------------------------------------------------------------

#include "Engine/Hydro/WaterSystem.hpp"
#include "Engine/Terrain/TerrainMesh.hpp"   // flat grid generator, reused for the water plane
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Nightbloom
{
	bool WaterSystem::Initialize(Renderer* renderer)
	{
		if (!renderer)
		{
			LOG_ERROR("WaterSystem::Initialize — null renderer");
			return false;
		}

		m_Renderer = renderer;
		m_Resources = renderer->GetResourceManager();

		if (!m_Resources)
		{
			LOG_ERROR("WaterSystem::Initialize — renderer subsystems not ready");
			return false;
		}

		LOG_INFO("WaterSystem initialized");
		return true;
	}

	bool WaterSystem::Regenerate(const WaterDesc& desc)
	{
		m_Ready = false;
		m_SkipNextDraw = true;

		m_Renderer->WaitForIdle();

		bool needsMesh = !m_MeshBuilt
			|| desc.resolution != m_CurrentDesc.resolution
			|| desc.worldSize != m_CurrentDesc.worldSize;

		if (needsMesh)
		{
			if (!BuildPlaneMesh(desc.resolution, desc.worldSize))
			{
				LOG_ERROR("WaterSystem::Regenerate — failed to build plane mesh");
				return false;
			}
		}

		m_CurrentDesc = desc;
		m_Ready = true;
		return true;
	}

	void WaterSystem::SubmitDraw(DrawList& drawList) const
	{
		if (m_SkipNextDraw) { m_SkipNextDraw = false; return; }

		if (!m_Ready || !m_VertexBuffer || !m_IndexBuffer || m_IndexCount == 0)
			return;

		DrawCommand cmd;
		cmd.pipeline = PipelineType::Water;
		cmd.vertexBuffer = m_VertexBuffer.get();
		cmd.indexBuffer = m_IndexBuffer.get();
		cmd.indexCount = m_IndexCount;
		cmd.instanceCount = 1;

		// The plane mesh is generated flat at Y=0; the model matrix lifts it to
		// the water surface height and centres it.
		glm::vec3 origin = m_CurrentDesc.position;
		origin.y = m_CurrentDesc.waterY;

		cmd.hasPushConstants = true;
		cmd.pushConstants.model = glm::translate(glm::mat4(1.0f), origin);
		// customData = (waveAmplitude, waveSpeed, fresnelPower, alpha).
		// Wave time itself comes from FrameUniforms.time.x (set 0).
		cmd.pushConstants.customData = glm::vec4(
			m_CurrentDesc.waveAmplitude,
			m_CurrentDesc.waveSpeed,
			m_CurrentDesc.fresnelPower,
			m_CurrentDesc.alpha);

		drawList.AddCommand(cmd);
	}

	void WaterSystem::Shutdown()
	{
		if (m_Renderer)
		{
			m_Renderer->WaitForIdle();
			// Clear the Renderer's back-pointer so its reflection pass stops
			// running and never dereferences this system after destruction
			// (same convention as Firefly/Cloud systems).
			m_Renderer->SetWaterSystem(nullptr);
		}

		m_VertexBuffer.reset();
		m_IndexBuffer.reset();
		m_IndexCount = 0;
		m_MeshBuilt = false;
		m_Ready = false;

		LOG_INFO("WaterSystem shut down");
	}

	bool WaterSystem::BuildPlaneMesh(uint32_t resolution, float worldSize)
	{
		// A flat grid at Y=0 — identical to what the terrain mesh generator
		// produces, so reuse it rather than duplicate the grid code. Resolution
		// can stay low since the water surface is flat (waves are shader-side).
		TerrainMeshData data = TerrainMesh::Generate(resolution, worldSize);

		if (data.vertices.empty() || data.indices.empty())
		{
			LOG_ERROR("WaterSystem: mesh generation returned empty data");
			return false;
		}

		VkDeviceSize vbSize = data.vertices.size() * sizeof(VertexPNT);
		m_VertexBuffer = m_Resources->CreateVertexBufferUnique("water_vb", vbSize, false);
		if (!m_VertexBuffer ||
			!m_VertexBuffer->UploadData(data.vertices.data(), vbSize, 0, m_Resources->GetTransferCommandPool()))
		{
			LOG_ERROR("WaterSystem: failed to create/upload vertex buffer");
			m_VertexBuffer.reset();
			return false;
		}

		VkDeviceSize ibSize = data.indices.size() * sizeof(uint32_t);
		m_IndexBuffer = m_Resources->CreateIndexBufferUnique("water_ib", ibSize, false);
		if (!m_IndexBuffer ||
			!m_IndexBuffer->UploadData(data.indices.data(), ibSize, 0, m_Resources->GetTransferCommandPool()))
		{
			LOG_ERROR("WaterSystem: failed to create/upload index buffer");
			m_IndexBuffer.reset();
			return false;
		}

		m_IndexCount = data.indexCount();
		m_MeshBuilt = true;

		LOG_INFO("WaterSystem: plane mesh built ({} vertices, {} indices)",
			data.vertexCount(), data.indexCount());
		return true;
	}

} // namespace Nightbloom
