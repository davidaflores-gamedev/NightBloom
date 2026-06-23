// Panels/TerrainPanel.cpp
#include "TerrainEditorPanel.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/NoiseTextureGenerator.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <imgui.h>

namespace Nightbloom
{
    // =========================================================================
    // Public
    // =========================================================================

    void TerrainPanel::Cleanup()
    {
        if (m_TerrainInitialized)
        {
            m_Terrain.Shutdown();
            m_TerrainInitialized = false;
        }
    }

    void TerrainPanel::Draw(EditorContext& ctx)
    {
        if (!isOpen) return;

        ImGui::Begin("Terrain", &isOpen);

        if (!ctx.renderer)
        {
            ImGui::TextDisabled("No renderer available.");
            ImGui::End();
            return;
        }

        // Lazy init on first draw
        if (!EnsureInitialized(ctx.renderer))
        {
            ImGui::TextDisabled("TerrainSystem failed to initialize.");
            ImGui::End();
            return;
        }

        bool changed = false;

        // ---- Grid settings --------------------------------------------------
        if (ImGui::CollapsingHeader("Grid", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* resOptions[] = { "32", "64", "128", "256", "512" };
            if (ImGui::Combo("Resolution", &m_ResolutionIndex, resOptions, 5))
                changed = true;

            if (ImGui::SliderFloat("World Size", &m_WorldSize, 50.0f, 1000.0f)) changed = true;
            if (ImGui::SliderFloat("Height Scale", &m_HeightScale, 1.0f, 200.0f)) changed = true;
        }

        // ---- Heightmap noise settings ----------------------------------------
        if (ImGui::CollapsingHeader("Heightmap Noise", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* noiseTypes[] = { "Perlin", "Worley", "PerlinWorley" };
            if (ImGui::Combo("Noise Type", &m_NoiseType, noiseTypes, 3))
                changed = true;

            const char* hmapResOptions[] = { "128", "256", "512", "1024" };
            if (ImGui::Combo("Heightmap Res", &m_HeightmapRes, hmapResOptions, 4))
                changed = true;

            if (ImGui::SliderInt("Octaves", &m_Octaves, 1, 8))     changed = true;
            if (ImGui::SliderFloat("Frequency", &m_Frequency, 0.5f, 16.0f)) changed = true;
            if (ImGui::SliderFloat("Persistence", &m_Persistence, 0.1f, 1.0f)) changed = true;
            if (ImGui::SliderFloat("Lacunarity", &m_Lacunarity, 1.0f, 4.0f)) changed = true;
            if (ImGui::SliderInt("Seed", &m_Seed, 0, 9999))  changed = true;
        }

        // ---- Transform -------------------------------------------------------
        if (ImGui::CollapsingHeader("Transform"))
        {
            if (ImGui::DragFloat3("Position", m_Position, 1.0f))
                changed = true;
        }

        ImGui::Separator();

        if (ImGui::Button("Regenerate Now", ImVec2(-1, 0)))
            m_Terrain.Regenerate(BuildDesc());

        if (changed)
            m_PendingDirty = true;

        // ---- Stats -----------------------------------------------------------
        ImGui::Separator();
        if (m_Terrain.IsReady())
        {
            const TerrainDesc& d = m_Terrain.GetDesc();
            uint32_t verts = d.resolution * d.resolution;
            uint32_t tris = (d.resolution - 1) * (d.resolution - 1) * 2;
            ImGui::TextDisabled("Vertices: %u  |  Triangles: %u", verts, tris);
            ImGui::TextDisabled("Heightmap: %ux%u  |  Height scale: %.1f",
                d.noise.width, d.noise.height, d.heightScale);
        }
        else
        {
            ImGui::TextDisabled("Terrain not ready.");
        }

        ImGui::End();

        // ---- Regen (outside Begin/End — may call WaitForIdle) ----------------
        if (changed)
        {
            // If slider was just released (not currently being dragged), regen now
            if (!ImGui::IsAnyItemActive())
            {
                m_Terrain.Regenerate(BuildDesc());
            }
            else
            {
                // Still dragging — mark pending so we regen on release
                m_PendingDirty = true;
            }
        }

        // Fire regen the frame after the slider is released
        if (m_PendingDirty && !ImGui::IsAnyItemActive())
        {
            m_Terrain.Regenerate(BuildDesc());
            m_PendingDirty = false;
        }
    }

    // =========================================================================
    // Private helpers
    // =========================================================================

    bool TerrainPanel::EnsureInitialized(Renderer* renderer)
    {
        if (m_TerrainInitialized) return true;

        if (!m_Terrain.Initialize(renderer))
        {
            LOG_ERROR("TerrainPanel: TerrainSystem::Initialize failed");
            return false;
        }

        m_TerrainInitialized = true;
        m_Terrain.Regenerate(BuildDesc());
        //m_TerrainDirty = false;
        m_PendingDirty = false;

        return true;
    }

    TerrainDesc TerrainPanel::BuildDesc() const
    {
        const uint32_t resValues[] = { 32, 64, 128, 256, 512 };
        const uint32_t hmapResValues[] = { 128, 256, 512, 1024 };

        TerrainDesc desc;
        desc.resolution = resValues[m_ResolutionIndex];
        desc.worldSize = m_WorldSize;
        desc.heightScale = m_HeightScale;
        desc.position = glm::vec3(m_Position[0], m_Position[1], m_Position[2]);

        desc.noise.width = hmapResValues[m_HeightmapRes];
        desc.noise.height = hmapResValues[m_HeightmapRes];
        desc.noise.depth = 1;
        desc.noise.noiseType = static_cast<NoiseType>(m_NoiseType);
        desc.noise.octaves = static_cast<uint32_t>(m_Octaves);
        desc.noise.frequency = m_Frequency;
        desc.noise.persistence = m_Persistence;
        desc.noise.lacunarity = m_Lacunarity;
        desc.noise.seed = static_cast<uint32_t>(m_Seed);
        desc.noise.debugName = "TerrainHeightmap";

        return desc;
    }

} // namespace Nightbloom
