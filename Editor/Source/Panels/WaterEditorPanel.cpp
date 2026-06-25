// Panels/WaterEditorPanel.cpp
#include "WaterEditorPanel.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <imgui.h>

namespace Nightbloom
{
    void WaterEditorPanel::Cleanup()
    {
        if (m_Initialized)
        {
            m_Water.Shutdown();
            m_Initialized = false;
        }
    }

    void WaterEditorPanel::Draw(EditorContext& ctx)
    {
        if (!isOpen) return;

        ImGui::Begin("Water", &isOpen);

        if (!ctx.renderer)
        {
            ImGui::TextDisabled("No renderer available.");
            ImGui::End();
            return;
        }

        if (!EnsureInitialized(ctx.renderer))
        {
            ImGui::TextDisabled("WaterSystem failed to initialize.");
            ImGui::End();
            return;
        }

        WaterDesc& desc = m_Water.GetDesc();

        // Surface placement — waterY is read live by the reflection pass
        // (mirror plane) and the draw's model matrix, so no rebuild needed.
        if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat("Water Y", &desc.waterY, 0.1f);
            ImGui::DragFloat2("Center XZ", &desc.position.x, 0.5f);

            // worldSize changes the plane mesh — needs a rebuild (Regenerate).
            float worldSize = desc.worldSize;
            if (ImGui::DragFloat("World Size", &worldSize, 1.0f, 1.0f, 2000.0f))
            {
                WaterDesc next = desc;
                next.worldSize = worldSize;
                m_Water.Regenerate(next);
            }
        }

        // Surface look — all live (ride in the draw push constant).
        if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Wave Amplitude", &desc.waveAmplitude, 0.0f, 0.25f);
            ImGui::SliderFloat("Wave Speed", &desc.waveSpeed, 0.0f, 3.0f);
            ImGui::SliderFloat("Fresnel Power", &desc.fresnelPower, 0.5f, 8.0f);
            ImGui::SliderFloat("Opacity", &desc.alpha, 0.0f, 1.0f);
        }

        ImGui::Separator();
        ImGui::TextDisabled("Reflects terrain/meshes/grass. Refraction +");
        ImGui::TextDisabled("tunable colors are deferred (Phase B/C).");

        ImGui::End();
    }

    bool WaterEditorPanel::EnsureInitialized(Renderer* renderer)
    {
        if (m_Initialized) return true;

        if (!m_Water.Initialize(renderer))
        {
            LOG_ERROR("WaterEditorPanel: WaterSystem::Initialize failed");
            return false;
        }

        if (!m_Water.Regenerate(m_Water.GetDesc()))
        {
            LOG_ERROR("WaterEditorPanel: WaterSystem::Regenerate failed");
            return false;
        }

        renderer->SetWaterSystem(&m_Water);

        m_Initialized = true;
        return true;
    }

} // namespace Nightbloom
