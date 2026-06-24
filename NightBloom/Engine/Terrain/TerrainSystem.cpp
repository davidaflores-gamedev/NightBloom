//------------------------------------------------------------------------------
// TerrainSystem.cpp
//------------------------------------------------------------------------------

#include "Engine/Terrain/TerrainSystem.hpp"
#include "Engine/Terrain/TerrainMesh.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanDescriptorManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanTexture.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Renderer/NoiseTextureGenerator.hpp"
#include "Engine/Renderer/Components/ComputeDispatcher.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Nightbloom
{
    bool TerrainSystem::Initialize(Renderer* renderer)
    {
        if (!renderer)
        {
            LOG_ERROR("TerrainSystem::Initialize — null renderer");
            return false;
        }

        m_Renderer = renderer;
        m_Resources = renderer->GetResourceManager();
        m_DescriptorManager = renderer->GetDescriptorManager();

        if (!m_Resources || !m_DescriptorManager)
        {
            LOG_ERROR("TerrainSystem::Initialize — renderer subsystems not ready");
            return false;
        }

        // Grab the default white texture for albedo (ResourceManager owns it)
        m_AlbedoTexture = m_Resources->GetTexture("default_white");
        if (m_AlbedoTexture)
        {
            // The default texture should already have a descriptor set
            // (ResourceManager::CreateDefaultTextures allocates one)
            m_AlbedoDescriptorSet = m_AlbedoTexture->GetDescriptorSet();
        }
        else
        {
            LOG_WARN("TerrainSystem: no default_white texture — albedo will be unbound");
        }

        InitializeTerrainMaterials();

        LOG_INFO("TerrainSystem initialized");
        return true;
    }

    bool TerrainSystem::InitializeTerrainMaterials()
    {
        m_GrassTexture = m_Resources->LoadTexture( "GrassAlbedo", "Grass/GrassAlbedo");
        m_DirtTexture = m_Resources->LoadTexture( "DirtAlbedo", "Dirt/DirtAlbedo");
        m_RockTexture = m_Resources->LoadTexture( "RockAlbedo", "Rock/RockAlbedo");

        if (!m_GrassTexture || !m_DirtTexture || !m_RockTexture)
        {
            LOG_WARN("TerrainSystem: one or more terrain textures missing");
            return false;
        }

        m_TerrainTextureSet = m_DescriptorManager->AllocateTextureDescriptorSet();
        if (m_TerrainTextureSet == VK_NULL_HANDLE)
        {
            LOG_ERROR("TerrainSystem: failed to allocate terrain texture set");
            return false;
        }

        m_DescriptorManager->UpdateTextureSet(
            m_TerrainTextureSet,
            static_cast<VulkanTexture*>(m_GrassTexture),
            0);

        m_DescriptorManager->UpdateTextureSet(
            m_TerrainTextureSet,
            static_cast<VulkanTexture*>(m_DirtTexture),
            1);

        m_DescriptorManager->UpdateTextureSet(
            m_TerrainTextureSet,
            static_cast<VulkanTexture*>(m_RockTexture),
            2);

        return true;
    }

    // =========================================================================
    // Regenerate
    // =========================================================================
    bool TerrainSystem::Regenerate(const TerrainDesc& desc)
    {
        m_Ready = false;
        m_SkipNextDraw = true;

        // Wait for any in-flight frames to finish before touching GPU resources
        m_Renderer->WaitForIdle();

        // ---- Rebuild grid mesh if resolution or size changed ----------------
        bool needsMesh = !m_MeshBuilt
            || desc.resolution != m_CurrentDesc.resolution
            || desc.worldSize != m_CurrentDesc.worldSize;

        if (needsMesh)
        {
            if (!BuildGridMesh(desc.resolution, desc.worldSize))
            {
                LOG_ERROR("TerrainSystem::Regenerate — failed to build grid mesh");
                return false;
            }
        }

        // ---- Regenerate heightmap, but only if noise params actually changed
        // (e.g. an LOD-driven resolution change has no effect on the noise
        // texture — regenerating it would be a wasted compute dispatch)
        bool needsHeightmap = !m_Heightmap
            || desc.noise.width != m_CurrentDesc.noise.width
            || desc.noise.height != m_CurrentDesc.noise.height
            || desc.noise.noiseType != m_CurrentDesc.noise.noiseType
            || desc.noise.octaves != m_CurrentDesc.noise.octaves
            || desc.noise.frequency != m_CurrentDesc.noise.frequency
            || desc.noise.persistence != m_CurrentDesc.noise.persistence
            || desc.noise.lacunarity != m_CurrentDesc.noise.lacunarity
            || desc.noise.seed != m_CurrentDesc.noise.seed;

        if (needsHeightmap)
        {
            DestroyHeightmap();

            NoiseTextureGenerator* noiseGen = m_Renderer->GetNoiseGenerator();
            ComputeDispatcher* dispatch = m_Renderer->GetComputeDispatcher();

            if (!noiseGen || !dispatch)
            {
                LOG_ERROR("TerrainSystem::Regenerate — noise generator not available");
                return false;
            }

            // Force depth=1 — we need a true 2D texture that the vertex shader
            // can sample with sampler2D (3D textures are not valid here)
            NoiseTextureDesc noiseDesc = desc.noise;
            noiseDesc.depth = 1;
            noiseDesc.debugName = "TerrainHeightmap";

            m_Heightmap = noiseGen->Generate(noiseDesc, dispatch);
            if (!m_Heightmap)
            {
                LOG_ERROR("TerrainSystem::Regenerate — noise generation failed");
                return false;
            }

            // ---- Allocate / update heightmap descriptor set ----------------
            // We allocate a fresh set each time. Old set is reclaimed by the pool
            // on the next pool reset (or on Shutdown via vkResetDescriptorPool).
            m_HeightmapDescriptorSet = m_DescriptorManager->AllocateHeightmapSet();
            if (m_HeightmapDescriptorSet == VK_NULL_HANDLE)
            {
                LOG_ERROR("TerrainSystem::Regenerate — failed to allocate heightmap descriptor set");
                return false;
            }

            m_DescriptorManager->UpdateHeightmapSet(m_HeightmapDescriptorSet, m_Heightmap);

            LOG_INFO("TerrainSystem: heightmap regenerated ({}x{}, scale={:.1f})",
                noiseDesc.width, noiseDesc.height, desc.heightScale);
        }

        m_CurrentDesc = desc;
        m_Ready = true;

        return true;
    }

    // =========================================================================
    // UpdateLOD
    // =========================================================================
    void TerrainSystem::UpdateLOD(const glm::vec3& cameraPosition, uint32_t baseResolution)
    {
        if (!m_Ready) return;

        float distance = glm::length(cameraPosition - m_CurrentDesc.position);
        uint32_t targetResolution = SelectLODResolution(distance, baseResolution, m_CurrentDesc.worldSize);

        if (targetResolution != m_CurrentDesc.resolution)
        {
            TerrainDesc desc = m_CurrentDesc;
            desc.resolution = targetResolution;
            Regenerate(desc);
        }
    }

    uint32_t TerrainSystem::SelectLODResolution(float distance, uint32_t baseResolution, float worldSize) const
    {
        uint32_t tier1Res = std::max(baseResolution / 2u, 32u);
        uint32_t tier2Res = std::max(baseResolution / 4u, 32u);
        uint32_t tier3Res = 32u;

        float b1 = worldSize * 1.0f;
        float b2 = worldSize * 2.0f;
        float b3 = worldSize * 4.0f;

        // Hysteresis: widen the boundary in the direction that keeps us in
        // our current tier, so hovering near a threshold doesn't flicker.
        const float kHysteresis = 0.15f;
        bool inTier0 = (m_CurrentDesc.resolution == baseResolution);
        bool inTier1 = !inTier0 && (m_CurrentDesc.resolution == tier1Res);
        bool inTier2 = !inTier0 && !inTier1 && (m_CurrentDesc.resolution == tier2Res);

        auto adjust = [kHysteresis](float boundary, bool currentlyCloser)
        {
            return currentlyCloser ? boundary * (1.0f + kHysteresis) : boundary * (1.0f - kHysteresis);
        };

        float adjB1 = adjust(b1, inTier0);
        float adjB2 = adjust(b2, inTier0 || inTier1);
        float adjB3 = adjust(b3, inTier0 || inTier1 || inTier2);

        if (distance < adjB1) return baseResolution;
        if (distance < adjB2) return tier1Res;
        if (distance < adjB3) return tier2Res;
        return tier3Res;
    }

    // =========================================================================
    // SubmitDraw
    // =========================================================================
    void TerrainSystem::SubmitDraw(DrawList& drawList) const
    {
        if (m_SkipNextDraw) { m_SkipNextDraw = false; return; }

        if (!m_Ready || !m_VertexBuffer || !m_IndexBuffer || m_IndexCount == 0)
            return;

        DrawCommand cmd;
        cmd.pipeline = PipelineType::Terrain;
        cmd.vertexBuffer = m_VertexBuffer.get();
        cmd.indexBuffer = m_IndexBuffer.get();
        cmd.indexCount = m_IndexCount;
        cmd.instanceCount = 1;

        // Push constants: model matrix, heightScale, texelSize
        float texelSize = 1.0f / static_cast<float>(m_CurrentDesc.resolution - 1);
        float gridTexelSize = 1.0f / static_cast<float>(m_CurrentDesc.resolution - 1);
        float heightmapTexelSize = 1.0f / static_cast<float>(m_CurrentDesc.noise.width - 1);

        cmd.hasPushConstants = true;
        cmd.pushConstants.model = glm::translate(glm::mat4(1.0f), m_CurrentDesc.position);
        cmd.pushConstants.customData = glm::vec4(
            m_CurrentDesc.heightScale,
            texelSize,
            m_CurrentDesc.worldSize,
            heightmapTexelSize
        );

        // Albedo texture (set 1) — use default white if available
        //if (m_AlbedoTexture)
        //{
        //    cmd.textures.push_back(m_AlbedoTexture);
        //}

        // Heightmap descriptor set (set 4)
        cmd.heightmapDescriptorSet = m_HeightmapDescriptorSet;
        cmd.textureDescriptorSet = m_TerrainTextureSet;

        drawList.AddCommand(cmd);

    }

    // =========================================================================
    // Shutdown
    // =========================================================================
    void TerrainSystem::Shutdown()
    {
        if (m_Renderer)
        {
            m_Renderer->WaitForIdle();
        }

        DestroyHeightmap();

        m_VertexBuffer.reset();
        m_IndexBuffer.reset();
        m_IndexCount = 0;
        m_MeshBuilt = false;
        m_Ready = false;

        LOG_INFO("TerrainSystem shut down");
    }

    // =========================================================================
    // Private helpers
    // =========================================================================

    bool TerrainSystem::BuildGridMesh(uint32_t resolution, float worldSize)
    {
        LOG_INFO("TerrainSystem: building {}x{} grid (worldSize={:.1f})",
            resolution, resolution, worldSize);

        TerrainMeshData data = TerrainMesh::Generate(resolution, worldSize);

        if (data.vertices.empty() || data.indices.empty())
        {
            LOG_ERROR("TerrainSystem: TerrainMesh::Generate returned empty data");
            return false;
        }

        // ---- Vertex buffer --------------------------------------------------
        VkDeviceSize vbSize = data.vertices.size() * sizeof(VertexPNT);
        m_VertexBuffer = m_Resources->CreateVertexBufferUnique("terrain_vb", vbSize, false);
        if (!m_VertexBuffer)
        {
            LOG_ERROR("TerrainSystem: failed to create vertex buffer");
            return false;
        }

        if (!m_VertexBuffer->UploadData(
            data.vertices.data(), vbSize, 0,
            m_Resources->GetTransferCommandPool()))
        {
            LOG_ERROR("TerrainSystem: failed to upload vertex data");
            m_VertexBuffer.reset();
            return false;
        }

        // ---- Index buffer ---------------------------------------------------
        VkDeviceSize ibSize = data.indices.size() * sizeof(uint32_t);
        m_IndexBuffer = m_Resources->CreateIndexBufferUnique("terrain_ib", ibSize, false);
        if (!m_IndexBuffer)
        {
            LOG_ERROR("TerrainSystem: failed to create index buffer");
            return false;
        }

        if (!m_IndexBuffer->UploadData(
            data.indices.data(), ibSize, 0,
            m_Resources->GetTransferCommandPool()))
        {
            LOG_ERROR("TerrainSystem: failed to upload index data");
            m_IndexBuffer.reset();
            return false;
        }

        m_IndexCount = data.indexCount();
        m_MeshBuilt = true;

        LOG_INFO("TerrainSystem: grid mesh built ({} vertices, {} indices)",
            data.vertexCount(), data.indexCount());

        return true;
    }

    void TerrainSystem::DestroyHeightmap()
    {
        // The heightmap texture is owned by this system (not ResourceManager)
        if (m_Heightmap)
        {
            delete m_Heightmap;
            m_Heightmap = nullptr;
        }
        // The descriptor set is reclaimed by the pool — no explicit free needed
        // (pool was created with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
        // so it could be freed, but we match the pattern used by NoiseTextureGenerator)
        m_HeightmapDescriptorSet = VK_NULL_HANDLE;
    }

} // namespace Nightbloom