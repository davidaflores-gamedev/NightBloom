// Panels/ViewportPanel.cpp
#include "ViewportPanel.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include <imgui.h>

namespace Nightbloom
{
    void ViewportPanel::Draw(EditorContext& ctx)
    {
        ImGui::Begin("Viewport");

        if (ImGui::Button("Perspective")) {}
        ImGui::SameLine();
        if (ImGui::Button("Top")) {}
        ImGui::SameLine();
        if (ImGui::Button("Side")) {}

        ImGui::Separator();

        if (ctx.renderer)
        {
            if (ImGui::Button("Toggle Pipeline (P)"))
                ctx.renderer->TogglePipeline();
            ImGui::SameLine();
            if (ImGui::Button("Reload Shaders (R)"))
                ctx.renderer->ReloadShaders();
        }

        ImGui::Separator();

        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        ImGui::Text("3D Viewport (%dx%d)", (int)viewportSize.x, (int)viewportSize.y);

        if (ctx.projectName)
            ImGui::Text("Current Project: %s", ctx.projectName->c_str());

        if (isPlayMode)
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "PLAYING");

        ImGui::End();
    }
} // namespace Nightbloom