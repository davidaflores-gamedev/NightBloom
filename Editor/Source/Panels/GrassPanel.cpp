// Panels/GrassPanel.cpp
#include "GrassPanel.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <imgui.h>

namespace Nightbloom
{
    // =========================================================================
    // Public
    // =========================================================================

    void GrassPanel::Cleanup()
    {
        if (m_GrassInitialized)
        {
            m_Grass.Shutdown();
            m_GrassInitialized = false;
        }
    }

    void GrassPanel::Draw(EditorContext& ctx, const TerrainSystem& terrain)
    {
        if (!isOpen) return;

        ImGui::Begin("Grass", &isOpen);

        if (!ctx.renderer)
        {
            ImGui::TextDisabled("No renderer available.");
            ImGui::End();
            return;
        }

        if (!terrain.IsReady())
        {
            ImGui::TextDisabled("Waiting on terrain...");
            ImGui::End();
            return;
        }

        // Lazy init on first draw
        if (!EnsureInitialized(ctx.renderer))
        {
            ImGui::TextDisabled("GrassSystem failed to initialize.");
            ImGui::End();
            return;
        }

        bool changed = false;

        // ---- Blade shape ------------------------------------------------------
        if (ImGui::CollapsingHeader("Blade Shape", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::SliderInt("Segments", &m_Segments, 1, 8)) changed = true;
            if (ImGui::SliderFloat("Base Half-Width", &m_BaseHalfWidth, 0.01f, 0.3f)) changed = true;
            if (ImGui::SliderFloat("Taper Power", &m_TaperPower, 0.5f, 4.0f)) changed = true;
            if (ImGui::SliderFloat("Min Tip Width Fraction", &m_MinTipWidthFraction, 0.0f, 0.6f)) changed = true;
            if (ImGui::SliderFloat("Bend Amount", &m_BendAmount, 0.0f, 0.8f)) changed = true;
            if (ImGui::SliderFloat("Blade Height", &m_BladeHeight, 0.1f, 2.0f)) changed = true;
            if (ImGui::SliderFloat("Height Jitter", &m_HeightJitter, 0.0f, 1.0f)) changed = true;
        }

        // ---- Placement ----------------------------------------------------------
        if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::SliderFloat("Patch Size", &m_PatchSize, 5.0f, 100.0f)) changed = true;
            if (ImGui::SliderFloat("Candidate Spacing", &m_CandidateSpacing, 0.2f, 3.0f)) changed = true;
            if (ImGui::SliderFloat("Density Threshold", &m_DensityThreshold, 0.0f, 1.0f)) changed = true;
            if (ImGui::SliderFloat("Density Frequency", &m_DensityFrequency, 0.01f, 0.5f)) changed = true;
            if (ImGui::SliderInt("Density Octaves", &m_DensityOctaves, 1, 4)) changed = true;
            if (ImGui::SliderInt("Seed", &m_Seed, 0, 9999)) changed = true;
            if (ImGui::SliderFloat("Slope Threshold (deg)", &m_SlopeThresholdDeg, 5.0f, 89.0f)) changed = true;
            if (ImGui::SliderFloat("Slope Falloff (deg)", &m_SlopeFalloffDeg, 0.0f, 30.0f)) changed = true;
            if (ImGui::SliderFloat("Color Variation", &m_ColorVariation, 0.0f, 0.5f)) changed = true;
        }

        // ---- Wind ---------------------------------------------------------------
        if (ImGui::CollapsingHeader("Wind", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::SliderFloat("Wind Speed", &m_WindSpeed, 0.0f, 5.0f)) changed = true;
            if (ImGui::SliderFloat("Wind Strength", &m_WindStrength, 0.0f, 1.0f)) changed = true;
            if (ImGui::SliderFloat("Wind Frequency", &m_WindFrequency, 0.0f, 2.0f)) changed = true;
        }

        // ---- Distance visibility (RETIRED) ---------------------------------------
        if (ImGui::CollapsingHeader("Distance Visibility (retired)"))
        {
            ImGui::TextDisabled("Old width-boost hack — inert. Distant blades now\nanti-alias via MSAA + density instead of ballooning\ninto fat triangles. Left here for reference only.");
            if (ImGui::SliderFloat("Min Apparent Width", &m_MinApparentWidth, 0.001f, 0.1f, "%.4f")) changed = true;
            if (ImGui::SliderFloat("Max Width Boost", &m_MaxWidthBoost, 1.0f, 20.0f)) changed = true;
        }

        // ---- Distance LOD ---------------------------------------------------------
        if (ImGui::CollapsingHeader("Distance LOD", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Continuous, pop-free density falloff.");
            if (ImGui::SliderFloat("Full Distance", &m_LodFullDistance, 5.0f, 200.0f)) changed = true;
            if (ImGui::SliderFloat("Fade Distance", &m_LodFadeDistance, 20.0f, 400.0f)) changed = true;
            if (ImGui::SliderFloat("Fade Band (blades)", &m_LodFadeBandBlades, 1.0f, 128.0f, "%.0f")) changed = true;
            if (ImGui::SliderFloat("Mesh LOD Distance", &m_MeshLodDistance, 10.0f, 300.0f)) changed = true;
        }

        ImGui::Separator();

        if (ImGui::Button("Regenerate Now", ImVec2(-1, 0)))
            m_Grass.Regenerate(BuildDesc(terrain));

        // Terrain bounds drifting (e.g. terrain regenerated with a different
        // world size/position/height scale) should trigger a re-placement
        // even if no grass slider moved.
        const TerrainDesc& terrainDesc = terrain.GetDesc();
        bool terrainBoundsChanged =
            terrainDesc.position != m_LastTerrainPosition ||
            terrainDesc.worldSize != m_LastTerrainWorldSize ||
            terrainDesc.heightScale != m_LastTerrainHeightScale;

        if (changed || terrainBoundsChanged)
            m_PendingDirty = true;

        // ---- Stats --------------------------------------------------------------
        ImGui::Separator();
        if (m_Grass.IsReady())
        {
            ImGui::TextDisabled("Instances: %u  |  Patches: %u",
                m_Grass.GetActiveInstanceCount(), m_Grass.GetPatchCount());
        }
        else
        {
            ImGui::TextDisabled("Grass not ready.");
        }

        ImGui::End();

        // ---- Regen (outside Begin/End — may call WaitForIdle) -------------------
        if ((changed || terrainBoundsChanged) && !ImGui::IsAnyItemActive())
        {
            m_Grass.Regenerate(BuildDesc(terrain));
            m_PendingDirty = false;
        }

        // Fire regen the frame after the slider is released
        if (m_PendingDirty && !ImGui::IsAnyItemActive())
        {
            m_Grass.Regenerate(BuildDesc(terrain));
            m_PendingDirty = false;
        }

        m_LastTerrainPosition = terrainDesc.position;
        m_LastTerrainWorldSize = terrainDesc.worldSize;
        m_LastTerrainHeightScale = terrainDesc.heightScale;
    }

    // =========================================================================
    // Private helpers
    // =========================================================================

    bool GrassPanel::EnsureInitialized(Renderer* renderer)
    {
        if (m_GrassInitialized) return true;

        if (!m_Grass.Initialize(renderer))
        {
            LOG_ERROR("GrassPanel: GrassSystem::Initialize failed");
            return false;
        }

        m_GrassInitialized = true;
        // Terrain bounds aren't known yet without a TerrainSystem reference,
        // but Draw() always calls BuildDesc(terrain) on the same frame right
        // after this, since it only reaches here once terrain.IsReady().
        m_PendingDirty = true;

        return true;
    }

    GrassDesc GrassPanel::BuildDesc(const TerrainSystem& terrain) const
    {
        GrassDesc desc;
        desc.segments = static_cast<uint32_t>(m_Segments);
        desc.bladeBaseHalfWidth = m_BaseHalfWidth;
        desc.taperPower = m_TaperPower;
        desc.minTipWidthFraction = m_MinTipWidthFraction;
        desc.bendAmount = m_BendAmount;
        desc.bladeHeight = m_BladeHeight;
        desc.heightJitter = m_HeightJitter;

        desc.patchSize = m_PatchSize;
        desc.candidateSpacing = m_CandidateSpacing;
        desc.densityThreshold = m_DensityThreshold;
        desc.densityFrequency = m_DensityFrequency;
        desc.densityOctaves = static_cast<uint32_t>(m_DensityOctaves);

        desc.windSpeed = m_WindSpeed;
        desc.windStrength = m_WindStrength;
        desc.windFrequency = m_WindFrequency;

        desc.slopeThresholdDeg = m_SlopeThresholdDeg;
        desc.slopeFalloffDeg = m_SlopeFalloffDeg;
        desc.minApparentWidth = m_MinApparentWidth;
        desc.maxWidthBoost = m_MaxWidthBoost;
        desc.lodFullDistance = m_LodFullDistance;
        desc.lodFadeDistance = m_LodFadeDistance;
        desc.lodFadeBandBlades = m_LodFadeBandBlades;
        desc.meshLodDistance = m_MeshLodDistance;
        desc.colorVariation = m_ColorVariation;
        desc.seed = static_cast<uint32_t>(m_Seed);

        const TerrainDesc& terrainDesc = terrain.GetDesc();
        desc.terrainPosition = terrainDesc.position;
        desc.terrainWorldSize = terrainDesc.worldSize;
        desc.terrainHeightScale = terrainDesc.heightScale;

        return desc;
    }

} // namespace Nightbloom
