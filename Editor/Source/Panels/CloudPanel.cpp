// Panels/CloudPanel.cpp
#include "CloudPanel.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <imgui.h>

namespace Nightbloom
{
    void CloudPanel::Cleanup()
    {
        if (m_Initialized)
        {
            m_Clouds.Shutdown();
            m_Initialized = false;
        }
    }

    void CloudPanel::Draw(EditorContext& ctx)
    {
        if (!isOpen) return;

        ImGui::Begin("Clouds", &isOpen);

        if (!ctx.renderer)
        {
            ImGui::TextDisabled("No renderer available.");
            ImGui::End();
            return;
        }

        if (!EnsureInitialized(ctx.renderer))
        {
            ImGui::TextDisabled("CloudSystem failed to initialize.");
            ImGui::End();
            return;
        }

        CloudDesc& desc = m_Clouds.GetDesc();

        if (ImGui::CollapsingHeader("Layer", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat("Layer Min Y", &desc.layerMinY, 10.0f);
            ImGui::DragFloat("Layer Max Y", &desc.layerMaxY, 10.0f);
            ImGui::DragFloat3("Wind Direction", &desc.windDirection.x, 0.05f);
            ImGui::SliderFloat("Wind Speed", &desc.windSpeed, 0.0f, 50.0f);
        }

        if (ImGui::CollapsingHeader("Shape", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Coverage", &desc.coverage, 0.0f, 1.0f);
            ImGui::SliderFloat("Shape Scale", &desc.shapeScale, 0.0001f, 0.01f, "%.5f");
            ImGui::SliderFloat("Detail Scale", &desc.detailScale, 0.001f, 0.05f, "%.5f");
            ImGui::SliderFloat("Detail Strength", &desc.detailStrength, 0.0f, 1.0f);
        }

        if (ImGui::CollapsingHeader("Lighting / Density", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Density Multiplier", &desc.densityMultiplier, 0.0f, 5.0f);
            ImGui::SliderFloat("Extinction Coefficient", &desc.extinctionCoefficient, 0.0f, 5.0f);
            ImGui::SliderFloat("HG Anisotropy (g)", &desc.hgAnisotropy, -0.99f, 0.99f);
            ImGui::SliderInt("Step Count", &desc.stepCount, 16, 256);
        }

        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // The raymarch runs in a compute pass at this fraction of the
            // screen resolution (see CloudRaymarch.comp) - lower = faster,
            // higher = sharper. Doesn't need a noise Regenerate, just
            // resizes the result image.
            if (ImGui::SliderFloat("Resolution Scale", &desc.resolutionScale, 0.25f, 1.0f))
            {
                m_Clouds.ResizeResultImage(ctx.renderer->GetWidth(), ctx.renderer->GetHeight());
            }
            ImGui::TextDisabled("Lower = faster, less detailed");
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Noise (requires Regenerate)"))
        {
            const char* shapeResOptions[] = { "64", "128", "256" };
            ImGui::Combo("Shape Resolution", &m_ShapeResIndex, shapeResOptions, 3);
            ImGui::SliderInt("Shape Octaves", &m_ShapeOctaves, 1, 8);
            ImGui::SliderFloat("Shape Frequency", &m_ShapeFrequency, 1.0f, 16.0f);

            const char* detailResOptions[] = { "32", "64" };
            ImGui::Combo("Detail Resolution", &m_DetailResIndex, detailResOptions, 2);
            ImGui::SliderInt("Detail Octaves", &m_DetailOctaves, 1, 6);
            ImGui::SliderFloat("Detail Frequency", &m_DetailFrequency, 1.0f, 32.0f);

            ImGui::SliderInt("Seed", &m_Seed, 0, 9999);

            if (ImGui::Button("Regenerate Noise", ImVec2(-1, 0)))
            {
                m_Clouds.Regenerate(BuildDesc());
            }
        }

        ImGui::End();
    }

    bool CloudPanel::EnsureInitialized(Renderer* renderer)
    {
        if (m_Initialized) return true;

        if (!m_Clouds.Initialize(renderer))
        {
            LOG_ERROR("CloudPanel: CloudSystem::Initialize failed");
            return false;
        }

        if (!m_Clouds.Regenerate(BuildDesc()))
        {
            LOG_ERROR("CloudPanel: CloudSystem::Regenerate failed");
            return false;
        }

        renderer->SetCloudSystem(&m_Clouds);

        m_Initialized = true;
        return true;
    }

    CloudDesc CloudPanel::BuildDesc() const
    {
        const uint32_t shapeRes[] = { 64, 128, 256 };
        const uint32_t detailRes[] = { 32, 64 };

        // Start from the current live desc (preserves layer/wind/coverage/etc.
        // tuning across a noise regenerate) — defaults if not yet initialized.
        CloudDesc desc = m_Clouds.GetDesc();

        desc.shapeNoise.width = desc.shapeNoise.height = desc.shapeNoise.depth = shapeRes[m_ShapeResIndex];
        desc.shapeNoise.noiseType = NoiseType::PerlinWorley;
        desc.shapeNoise.octaves = static_cast<uint32_t>(m_ShapeOctaves);
        desc.shapeNoise.frequency = m_ShapeFrequency;
        desc.shapeNoise.persistence = 0.5f;
        desc.shapeNoise.lacunarity = 2.0f;
        desc.shapeNoise.seed = static_cast<uint32_t>(m_Seed);

        desc.detailNoise.width = desc.detailNoise.height = desc.detailNoise.depth = detailRes[m_DetailResIndex];
        desc.detailNoise.noiseType = NoiseType::Worley;
        desc.detailNoise.octaves = static_cast<uint32_t>(m_DetailOctaves);
        desc.detailNoise.frequency = m_DetailFrequency;
        desc.detailNoise.persistence = 0.5f;
        desc.detailNoise.lacunarity = 2.0f;
        desc.detailNoise.seed = static_cast<uint32_t>(m_Seed) + 1; // distinct from shape's seed

        return desc;
    }

} // namespace Nightbloom
