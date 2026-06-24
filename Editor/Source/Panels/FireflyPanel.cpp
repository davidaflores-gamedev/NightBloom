// Panels/FireflyPanel.cpp
#include "FireflyPanel.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <imgui.h>

namespace Nightbloom
{
    void FireflyPanel::Cleanup()
    {
        if (m_Initialized)
        {
            m_Firefly.Shutdown();
            m_Initialized = false;
        }
    }

    void FireflyPanel::Draw(EditorContext& ctx)
    {
        if (!isOpen) return;

        ImGui::Begin("Fireflies", &isOpen);

        if (!ctx.renderer)
        {
            ImGui::TextDisabled("No renderer available.");
            ImGui::End();
            return;
        }

        if (!EnsureInitialized(ctx.renderer))
        {
            ImGui::TextDisabled("FireflySystem failed to initialize.");
            ImGui::End();
            return;
        }

        FireflyParamsData& params = m_Firefly.GetParams();

        if (ImGui::CollapsingHeader("Swarm", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Agents: %d", m_AgentCount);

            ImGui::DragFloat3("Center", &params.boundsCenter.x, 1.0f);
            ImGui::DragFloat3("Extents", &params.boundsExtent.x, 1.0f, 1.0f, 500.0f);

            ImGui::Separator();
            ImGui::SliderInt("New Agent Count", &m_AgentCount, 50, 5000);
            if (ImGui::Button("Reinitialize Swarm", ImVec2(-1, 0)))
            {
                glm::vec3 center(params.boundsCenter);
                glm::vec3 extents(params.boundsExtent);

                m_Firefly.Shutdown(); // also clears Renderer's FireflySystem pointer
                m_Initialized = false;

                if (m_Firefly.Initialize(ctx.renderer, static_cast<uint32_t>(m_AgentCount), center, extents))
                {
                    m_Initialized = true;
                    ctx.renderer->SetFireflySystem(&m_Firefly);
                }
                else
                {
                    LOG_ERROR("FireflyPanel: failed to reinitialize FireflySystem");
                }
            }
        }

        if (ImGui::CollapsingHeader("Flocking", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Separation Force", &params.params1.x, 0.0f, 5.0f);
            ImGui::SliderFloat("Alignment Force", &params.params1.y, 0.0f, 5.0f);
            ImGui::SliderFloat("Cohesion Force", &params.params1.z, 0.0f, 5.0f);

            ImGui::Separator();
            ImGui::SliderFloat("Perception Radius", &params.params2.x, 0.0f, 100.0f);
            ImGui::SliderFloat("Separation Radius", &params.params2.y, 0.0f, 100.0f);
            ImGui::SliderFloat("Min Speed", &params.params2.z, 0.0f, 10.0f);
            ImGui::SliderFloat("Max Speed", &params.params2.w, 0.0f, 100.0f);

            ImGui::Separator();
            ImGui::SliderFloat("Wander Strength", &params.params3.y, 0.0f, 2.0f);
            ImGui::SliderFloat("Wander Force", &params.params3.z, 0.0f, 10.0f);
        }

        if (ImGui::CollapsingHeader("Blink", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Global Blink Speed Scale", &params.params4.x, 0.0f, 4.0f);
            ImGui::SliderFloat("Global Blink Amplitude", &params.params4.y, 0.0f, 2.0f);
        }

        ImGui::End();
    }

    bool FireflyPanel::EnsureInitialized(Renderer* renderer)
    {
        if (m_Initialized) return true;

        glm::vec3 center(m_Center[0], m_Center[1], m_Center[2]);
        glm::vec3 extents(m_Extents[0], m_Extents[1], m_Extents[2]);

        if (!m_Firefly.Initialize(renderer, static_cast<uint32_t>(m_AgentCount), center, extents))
        {
            LOG_ERROR("FireflyPanel: FireflySystem::Initialize failed");
            return false;
        }

        renderer->SetFireflySystem(&m_Firefly);

        m_Initialized = true;
        return true;
    }

} // namespace Nightbloom
