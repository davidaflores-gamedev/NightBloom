//------------------------------------------------------------------------------
// TerrainSystem.hpp
//
// Manages the terrain: heightmap generation, grid mesh, descriptor set,
// and draw command submission. Designed to be owned by EditorApp or
// the application layer — not by Renderer directly.
//
// Usage:
//   TerrainSystem terrain;
//   terrain.Initialize(renderer);
//   terrain.Regenerate(desc);          // call whenever noise params change
//   terrain.SubmitDraw(drawList);      // call each frame
//   terrain.Shutdown();
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/NoiseTextureGenerator.hpp"
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include "Engine/Renderer/Vulkan/VulkanTexture.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>

namespace Nightbloom
{
	class Renderer;
	class ResourceManager;
	class VulkanDescriptorManager;

	struct TerrainDesc
	{
        // Grid settings
        uint32_t resolution = 256;    // vertices per side
        float    worldSize = 200.0f; // world-space extent of the patch

        // Heightmap noise settings
        NoiseTextureDesc noise;       // passed directly to NoiseTextureGenerator

        // Height scaling
        float heightScale = 30.0f;    // world-space max height (Y displacement)

        // Transform
        glm::vec3 position = glm::vec3(0.0f); // centre of the terrain patch
	};

    class TerrainSystem
    {
    public:
        TerrainSystem() = default;
        ~TerrainSystem() = default;

        TerrainSystem(const TerrainSystem&) = delete;
        TerrainSystem& operator=(const TerrainSystem&) = delete;

        //----------------------------------------------------------------------
        // Initialize — call once after Renderer is ready
        //----------------------------------------------------------------------
        bool Initialize(Renderer* renderer);
        bool InitializeTerrainMaterials();

        //----------------------------------------------------------------------
        // Regenerate — safe to call every frame if params changed.
        // Waits for device idle. Rebuilds the grid mesh if resolution/worldSize
        // changed; only regenerates the heightmap if noise params changed.
        //----------------------------------------------------------------------
        bool Regenerate(const TerrainDesc& desc);

        //----------------------------------------------------------------------
        // UpdateLOD — call once per frame before SubmitDraw. Picks a grid
        // resolution tier based on distance from cameraPosition to the
        // terrain's center, never exceeding baseResolution (the user-selected
        // "near" resolution). Triggers a mesh-only Regenerate() if the tier
        // changes (heightmap is left untouched — see Regenerate()).
        //----------------------------------------------------------------------
        void UpdateLOD(const glm::vec3& cameraPosition, uint32_t baseResolution);

        //----------------------------------------------------------------------
        // SubmitDraw — add the terrain draw command to the frame draw list.
        // Call once per frame, after Regenerate (if needed) and before
        // Renderer::SubmitDrawList.
        //----------------------------------------------------------------------
        void SubmitDraw(DrawList& drawList) const;

        //----------------------------------------------------------------------
        // Shutdown — must be called before Renderer shuts down
        //----------------------------------------------------------------------
        void Shutdown();

        bool IsReady() const { return m_Ready; }

        // Read-back for editor display
        VulkanTexture* GetHeightmap() const { return m_Heightmap; }
        const TerrainDesc& GetDesc() const { return m_CurrentDesc; }

    private:
        bool BuildGridMesh(uint32_t resolution, float worldSize);
        uint32_t SelectLODResolution(float distance, uint32_t baseResolution, float worldSize) const;
        void DestroyHeightmap();

        Renderer* m_Renderer = nullptr;
        ResourceManager* m_Resources = nullptr;
        VulkanDescriptorManager* m_DescriptorManager = nullptr;

        // GPU resources
        std::unique_ptr<VulkanBuffer> m_VertexBuffer;
        std::unique_ptr<VulkanBuffer> m_IndexBuffer;
        uint32_t                      m_IndexCount = 0;

        VulkanTexture* m_Heightmap = nullptr;
        VkDescriptorSet  m_HeightmapDescriptorSet = VK_NULL_HANDLE;

        // Albedo placeholder — default white texture (from ResourceManager)
        VulkanTexture* m_AlbedoTexture = nullptr;
        VkDescriptorSet  m_AlbedoDescriptorSet = VK_NULL_HANDLE;

        Texture* m_GrassTexture = nullptr;
        Texture* m_DirtTexture = nullptr;
        Texture* m_RockTexture = nullptr;
        VkDescriptorSet m_TerrainTextureSet = VK_NULL_HANDLE;

        TerrainDesc m_CurrentDesc;
        bool        m_Ready = false;
        bool        m_MeshBuilt = false;
        mutable bool m_SkipNextDraw = false;
    };

} // Nightbloom