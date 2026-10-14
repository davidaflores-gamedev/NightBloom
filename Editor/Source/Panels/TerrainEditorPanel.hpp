// Panels/TerrainPanel.hpp
#pragma once
#include "../EditorContext.hpp"
#include "Engine/Terrain/TerrainSystem.hpp"
#include <imgui.h>

namespace Nightbloom
{
    class TerrainPanel
    {
    public:
        bool isOpen = true;

        void Draw(EditorContext& ctx);

        // Call before renderer shuts down (while Vulkan device is still alive)
        void Cleanup();

        void SubmitTerrainDraw(DrawList& drawList, const glm::vec3& cameraPosition)
        {
            if (m_TerrainInitialized && m_Terrain.IsReady())
            {
                m_Terrain.UpdateLOD(cameraPosition, ResolutionValues[m_ResolutionIndex]);
                m_Terrain.SubmitDraw(drawList);
            }
        }

        // For GrassPanel — foliage placement/height-sampling needs terrain's
        // world bounds and heightmap descriptor set, see GrassSystem.
        const TerrainSystem& GetTerrainSystem() const { return m_Terrain; }

        // Demo mode (EditorDemo.hpp): change the heightmap noise as if the sliders
        // were moved, so the panel shows the new values and the terrain regenerates.
        // Pass seed < 0 / frequency <= 0 to leave that parameter unchanged. Only
        // seed and frequency, not height scale: grass re-fits to height scale only
        // from GrassPanel::Draw, but reads the heightmap itself directly.
        void DemoSetNoise(int seed, float frequency)
        {
            if (seed >= 0) m_Seed = seed;
            if (frequency > 0.0f) m_Frequency = frequency;
            if (m_TerrainInitialized) m_Terrain.Regenerate(BuildDesc());
        }

    private:
        // Shared source of truth for the resolution dropdown, used by both
        // BuildDesc() and SubmitTerrainDraw()'s LOD base-resolution lookup.
        static constexpr uint32_t ResolutionValues[5] = { 32, 64, 128, 256, 512 };

        // TerrainSystem owned by this panel (holds GPU resources)
        TerrainSystem m_Terrain;
        bool          m_TerrainInitialized = false;

        bool          m_PendingDirty = false;

        // Grid settings
        int   m_ResolutionIndex = 3;   // 0=32 1=64 2=128 3=256 4=512
        float m_WorldSize = 200.0f;
        float m_HeightScale = 30.0f;

        // Heightmap noise settings
        int   m_NoiseType = 0;    // 0=Perlin 1=Worley 2=PerlinWorley
        int   m_Octaves = 6;
        float m_Frequency = 3.0f;
        float m_Persistence = 0.5f;
        float m_Lacunarity = 2.0f;
        int   m_Seed = 42;
        int   m_HeightmapRes = 1;    // 0=128 1=256 2=512 3=1024

        // Position
        float m_Position[3] = { 0.0f, 0.0f, 0.0f };

        TerrainDesc BuildDesc() const;
        bool        EnsureInitialized(Renderer* renderer);
    };

} // namespace Nightbloom