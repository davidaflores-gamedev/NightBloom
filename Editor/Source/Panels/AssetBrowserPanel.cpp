// Panels/AssetBrowserPanel.cpp
#include "AssetBrowserPanel.hpp"
#include <imgui.h>

namespace Nightbloom
{
    void AssetBrowserPanel::Draw(EditorContext& /*ctx*/)
    {
        ImGui::Begin("Asset Browser", &isOpen);

        if (ImGui::Button("Import Asset...")) { /* TODO: file dialog */ }
        ImGui::Separator();

        if (ImGui::TreeNode("Shaders"))
        {
            ImGui::Text("triangle.vert");
            ImGui::Text("triangle.frag");
            ImGui::Text("mesh.vert");
            ImGui::Text("mesh.frag");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Textures")) { ImGui::Text("No textures"); ImGui::TreePop(); }
        if (ImGui::TreeNode("Models")) { ImGui::Text("No models");   ImGui::TreePop(); }

        ImGui::End();
    }
} // namespace Nightbloom