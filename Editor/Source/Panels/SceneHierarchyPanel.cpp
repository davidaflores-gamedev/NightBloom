// Panels/SceneHierarchyPanel.cpp
#include "SceneHierarchyPanel.hpp"
#include "Engine/Core/Scene.hpp"
#include "Engine/Renderer/Light.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Model.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanTexture.hpp"  // complete type for VulkanTexture* -> Texture*
#include "Engine/Core/Logger/Logger.hpp"
#include "../EditorFileUtils.hpp"
#include <imgui.h>
#include <memory>
#include <filesystem>

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
            if (ImGui::MenuItem("Load Model...") && ctx.scene && ctx.renderer)
            {
                std::string path = Editor::EditorFileUtils::OpenFileDialog(
                    "glTF Models (*.gltf;*.glb)\0*.gltf;*.glb\0All Files (*.*)\0*.*\0");
                if (!path.empty())
                {
                    std::string name = std::filesystem::path(path).stem().string();
                    auto model = std::make_unique<Model>(name);
                    if (model->LoadFromFile(path, ctx.renderer->GetResourceManager(),
                        ctx.renderer->GetDescriptorManager()))
                    {
                        ResourceManager* res = ctx.renderer->GetResourceManager();
                        Texture* def = res ? res->GetTexture("default_white") : nullptr;
                        ctx.scene->AddObject(name, std::move(model), def);
                        LOG_INFO("Loaded model '{}' from {}", name, path);
                    }
                    else
                    {
                        LOG_WARN("Failed to load model from {}", path);
                    }
                }
            }
            if (ImGui::MenuItem("Primitive Cube") && ctx.scene && ctx.renderer)
            {
                Buffer* vb = ctx.renderer->GetTestVertexBuffer();
                Buffer* ib = ctx.renderer->GetTestIndexBuffer();
                uint32_t ic = ctx.renderer->GetTestIndexCount();
                if (vb && ib && ic > 0)
                {
                    auto cube = std::make_unique<MeshDrawable>(vb, ib, ic, PipelineType::Mesh);
                    ResourceManager* res = ctx.renderer->GetResourceManager();
                    if (res)
                        if (Texture* white = res->GetTexture("default_white"))
                            cube->AddTexture(white);
                    SceneObject* o = ctx.scene->AddPrimitive("Cube", std::move(cube));
                    if (o)
                    {
                        o->pipeline = PipelineType::Mesh;
                        // Metadata so the cube survives save/load (see SceneSerializer).
                        o->primitiveKind = PrimitiveKind::TestCube;
                        o->primitiveTexture = "default_white";
                    }
                    LOG_INFO("Added primitive cube");
                }
                else
                {
                    LOG_WARN("Test cube buffers unavailable");
                }
            }

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