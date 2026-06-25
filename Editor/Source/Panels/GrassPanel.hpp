// Panels/GrassPanel.hpp
#pragma once
#include "../EditorContext.hpp"
#include "Engine/Foliage/GrassSystem.hpp"
#include "Engine/Terrain/TerrainSystem.hpp"
#include <imgui.h>

namespace Nightbloom
{
    struct Frustum;

    class GrassPanel
    {
    public:
        bool isOpen = true;

        // Needs the terrain panel's TerrainSystem (world bounds + heightmap)
        // since GrassSystem has no CPU height query of its own — see
        // GrassSystem.hpp's header comment.
        void Draw(EditorContext& ctx, const TerrainSystem& terrain);

        // Call before renderer shuts down (while Vulkan device is still alive)
        void Cleanup();

        void SubmitGrassDraw(DrawList& drawList, const Frustum& frustum, const TerrainSystem& terrain,
            const glm::vec3& cameraPosition)
        {
            if (m_GrassInitialized && m_Grass.IsReady() && terrain.IsReady())
            {
                m_Grass.SubmitDraw(drawList, frustum, terrain.GetHeightmapDescriptorSet(), cameraPosition);
            }
        }

    private:
        GrassSystem m_Grass;
        bool        m_GrassInitialized = false;
        bool        m_PendingDirty = false;

        // Blade shape — width is a sizeable fraction of candidateSpacing
        // below so neighboring blades visually overlap (continuous cover)
        // rather than reading as isolated spikes over bare dirt.
        int   m_Segments = 4;
        float m_BaseHalfWidth = 0.1f;
        float m_TaperPower = 1.5f;
        float m_MinTipWidthFraction = 0.18f;

        // Height
        float m_BladeHeight = 0.6f;
        float m_HeightJitter = 0.3f;

        // Placement — a dense candidate grid (candidateSpacing apart) kept
        // or dropped per-candidate by a noise sample, instead of discrete
        // clump objects (an earlier pass used random clump centers; dropped
        // because it read as a uniform grid of separate dots rather than
        // continuous patchy cover — see GrassSystem.hpp's header comment).
        float m_PatchSize = 25.0f;
        float m_CandidateSpacing = 0.6f;
        float m_DensityThreshold = 0.5f;
        float m_DensityFrequency = 0.06f;
        int   m_DensityOctaves = 2;
        int   m_Seed = 1337;

        // Wind
        float m_WindSpeed = 1.0f;
        float m_WindStrength = 0.15f;
        float m_WindFrequency = 0.3f;

        // Cliff avoidance + color
        float m_SlopeThresholdDeg = 45.0f;
        float m_SlopeFalloffDeg = 10.0f;
        float m_ColorVariation = 0.15f;

        // Distance visibility (fake-it width boost, not true LOD — see
        // GrassDesc's header comment)
        float m_MinApparentWidth = 0.02f;
        float m_MaxWidthBoost = 8.0f;

        // Distance LOD (real instance-count reduction — see GrassSystem.hpp's
        // header comment)
        float m_LodMidDistance = 40.0f;
        float m_LodFarDistance = 90.0f;
        float m_LodMidFraction = 0.45f;
        float m_LodFarFraction = 0.15f;

        // Cached terrain bounds, to detect terrain changes that should
        // trigger a re-placement even if no grass slider moved.
        glm::vec3 m_LastTerrainPosition = glm::vec3(0.0f);
        float     m_LastTerrainWorldSize = 0.0f;
        float     m_LastTerrainHeightScale = 0.0f;

        GrassDesc BuildDesc(const TerrainSystem& terrain) const;
        bool      EnsureInitialized(Renderer* renderer);
    };

} // namespace Nightbloom
