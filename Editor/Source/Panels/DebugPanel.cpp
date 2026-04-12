// Panels/DebugPanel.cpp
#include "DebugPanel.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include <imgui.h>

namespace Nightbloom
{
    void DebugPanel::Draw(EditorContext& ctx)
    {
        ImGui::Begin("Debug Panel", &isOpen);

        ImGui::Text("Compute Pipeline Testing");
        ImGui::Separator();

        if (ImGui::Button("Run Compute Test", ImVec2(200, 30)))
        {
            if (ctx.renderer)
            {
                ctx.renderer->PrintComputeTestResults();
                m_ComputeTestRan = true;
            }
        }

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Runs a simple compute shader that multiplies 64 floats by 2.\nCheck the console/log for results.");

        ImGui::Spacing();
        ImGui::Text("The compute test:");
        ImGui::BulletText("Input:  1, 2, 3, ... 64");
        ImGui::BulletText("Output: 2, 4, 6, ... 128");

        if (m_ComputeTestRan)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                "Test executed - check console for results");
        }

        ImGui::Separator();
        ImGui::Text("Shader Tools");

        if (ImGui::Button("Reload All Shaders", ImVec2(200, 25)))
            if (ctx.renderer) ctx.renderer->ReloadShaders();

        if (ImGui::Button("Toggle Pipeline (P)", ImVec2(200, 25)))
            if (ctx.renderer) ctx.renderer->TogglePipeline();

        ImGui::Separator();
        ImGui::Text("Memory & Performance");
        ImGui::Text("Press F3 to toggle performance overlay");

        ImGui::End();
    }
} // namespace Nightbloom