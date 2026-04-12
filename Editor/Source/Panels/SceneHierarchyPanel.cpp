// Panels/SceneHierarchyPanel.cpp
#include "SceneHierarchyPanel.hpp"
#include "Engine/Core/Scene.hpp"
#include "Engine/Renderer/Light.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <imgui.h>

namespace Nightbloom
{
    void SceneHierarchyPanel::Draw(EditorContext& ctx)
    {
        ImGui::Begin("Scene Hierarchy", &isOpen);

        if (!ctx.scene)
        {
            ImGui::Text("No scene loaded");
            ImGui::End();
            return;
        }

        ImGuiTreeNodeFlags rootFlags =
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;

        if (ImGui::TreeNodeEx("Scene", rootFlags))
        {
            auto& objects = ctx.scene->GetObjects();
            int selectedIndex = ctx.scene->GetSelectedIndex();

            for (size_t i = 0; i < objects.size(); ++i)
            {
                auto& obj = objects[i];

                ImGuiTreeNodeFlags nodeFlags =
                    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                if (static_cast<int>(i) == selectedIndex)
                    nodeFlags |= ImGuiTreeNodeFlags_Selected;

                if (!obj.visible)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

                const char* icon = obj.model ? "[M]" : "[P]";
                ImGui::TreeNodeEx((void*)(intptr_t)i, nodeFlags,
                    "%s %s", icon, obj.name.c_str());

                if (ImGui::IsItemClicked())
                    ctx.scene->Select(static_cast<int>(i));

                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Toggle Visibility"))
                        obj.visible = !obj.visible;
                    if (ImGui::MenuItem("Rename..."))
                    { /* TODO */
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete"))
                    {
                        ctx.scene->RemoveObject(i);
                        ImGui::EndPopup();
                        if (!obj.visible) ImGui::PopStyleColor();
                        break;
                    }
                    ImGui::EndPopup();
                }

                if (!obj.visible)
                    ImGui::PopStyleColor();
            }

            ImGui::Separator();

            // Lights
            if (ImGui::TreeNodeEx("Lights", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto& lights = ctx.scene->GetLights();
                int selectedLightIndex = ctx.scene->GetSelectedLightIndex();

                for (size_t i = 0; i < lights.size(); ++i)
                {
                    auto& light = lights[i];

                    ImGuiTreeNodeFlags nodeFlags =
                        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                    if (static_cast<int>(i) == selectedLightIndex)
                        nodeFlags |= ImGuiTreeNodeFlags_Selected;

                    if (!light.enabled)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

                    const char* icon = (light.type == LightType::Directional) ? "[D]" : "[P]";
                    ImGui::TreeNodeEx((void*)(intptr_t)(1000 + i), nodeFlags,
                        "%s %s", icon, light.name.c_str());

                    if (ImGui::IsItemClicked())
                        ctx.scene->SelectLight(static_cast<int>(i));

                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Toggle Enabled"))
                            light.enabled = !light.enabled;
                        if (ImGui::MenuItem("Delete"))
                        {
                            ctx.scene->RemoveLight(i);
                            ImGui::EndPopup();
                            if (!light.enabled) ImGui::PopStyleColor();
                            break;
                        }
                        ImGui::EndPopup();
                    }

                    if (!light.enabled)
                        ImGui::PopStyleColor();
                }

                ImGui::TreePop();
            }

            ImGui::TreePop();
        }

        ImGui::Separator();

        // Add object
        if (ImGui::Button("+ Add Object"))
            ImGui::OpenPopup("AddObjectPopup");

        if (ImGui::BeginPopup("AddObjectPopup"))
        {
            if (ImGui::MenuItem("Load Model..."))
                LOG_INFO("Load model dialog - not yet implemented");
            if (ImGui::MenuItem("Primitive Cube"))
                LOG_INFO("Add cube - not yet implemented");

            ImGui::Separator();
            if (ImGui::MenuItem("Directional Light") && ctx.scene)
            {
                Light* l = ctx.scene->AddLight("Directional Light", LightType::Directional);
                l->direction = glm::vec3(0.0f, -1.0f, 0.0f);
                l->color = glm::vec3(1.0f);
                l->intensity = 1.0f;
                LOG_INFO("Added directional light");
            }
            if (ImGui::MenuItem("Point Light") && ctx.scene)
            {
                Light* l = ctx.scene->AddLight("Point Light", LightType::Point);
                l->position = glm::vec3(0.0f, 3.0f, 0.0f);
                l->color = glm::vec3(1.0f, 0.8f, 0.6f);
                l->intensity = 2.0f;
                LOG_INFO("Added point light");
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }
} // namespace Nightbloom