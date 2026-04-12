// Panels/InspectorPanel.cpp
#include "InspectorPanel.hpp"
#include "Engine/Core/Scene.hpp"
#include "Engine/Renderer/Material.hpp"
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

namespace Nightbloom
{
    void InspectorPanel::Draw(EditorContext& ctx)
    {
        ImGui::Begin("Inspector", &isOpen);

        if (!ctx.scene)
        {
            ImGui::Text("No scene");
            ImGui::End();
            return;
        }

        SceneObject* selected = ctx.scene->GetSelected();

        if (!selected)
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No object selected");
            ImGui::End();
            return;
        }

        // Name
        char nameBuf[256];
        strncpy(nameBuf, selected->name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            selected->name = nameBuf;

        ImGui::Checkbox("Visible", &selected->visible);
        ImGui::Separator();

        // Transform
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (selected->model)
            {
                glm::vec3 position = selected->GetPosition();
                glm::vec3 rotation = selected->GetRotation();
                glm::vec3 scale = selected->GetScale();
                glm::vec3 rotDeg = glm::degrees(rotation);
                bool changed = false;

                if (ImGui::DragFloat3("Position", &position.x, 0.1f))
                {
                    selected->SetPosition(position); changed = true;
                }

                if (ImGui::DragFloat3("Rotation", &rotDeg.x, 1.0f))
                {
                    selected->SetRotation(glm::radians(rotDeg)); changed = true;
                }

                if (ImGui::DragFloat3("Scale", &scale.x, 0.01f, 0.001f, 100.0f))
                {
                    selected->SetScale(scale); changed = true;
                }

                static float uniformScale = 1.0f;
                if (changed)
                    uniformScale = (scale.x + scale.y + scale.z) / 3.0f;

                if (ImGui::DragFloat("Uniform Scale", &uniformScale, 0.01f, 0.001f, 100.0f))
                    selected->SetScale(uniformScale);

                if (ImGui::Button("Reset Transform"))
                {
                    selected->SetPosition(glm::vec3(0.0f));
                    selected->SetRotation(glm::vec3(0.0f));
                    selected->SetScale(glm::vec3(1.0f));
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                    "(Primitive - transform not editable yet)");
            }
        }

        // Mesh info
        if (selected->model && ImGui::CollapsingHeader("Mesh Info"))
        {
            ImGui::Text("Meshes: %zu", selected->GetMeshCount());
            ImGui::Text("Vertices: %zu", selected->GetVertexCount());
            ImGui::Text("Indices: %zu", selected->GetIndexCount());

            if (ImGui::TreeNode("Meshes"))
            {
                const auto& meshes = selected->model->GetMeshes();
                for (size_t i = 0; i < meshes.size(); ++i)
                {
                    const auto& mesh = meshes[i];
                    ImGui::BulletText("%s: %u verts, %u indices",
                        mesh->GetName().c_str(),
                        mesh->GetVertexCount(),
                        mesh->GetIndexCount());
                }
                ImGui::TreePop();
            }
        }

        // Materials
        if (selected->model && ImGui::CollapsingHeader("Materials"))
        {
            const auto& materials = selected->model->GetMaterials();
            if (materials.empty())
            {
                ImGui::Text("No materials");
            }
            else
            {
                for (size_t i = 0; i < materials.size(); ++i)
                {
                    Material* mat = materials[i].get();
                    if (!mat) continue;
                    ImGui::PushID(static_cast<int>(i));

                    if (ImGui::TreeNode(mat->GetName().c_str()))
                    {
                        glm::vec4 albedo = mat->GetAlbedoColor();
                        if (ImGui::ColorEdit4("Albedo", &albedo.x))
                            mat->SetAlbedoColor(albedo);

                        float metallic = mat->GetMetallic();
                        if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f))
                            mat->SetMetallic(metallic);

                        float roughness = mat->GetRoughness();
                        if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f))
                            mat->SetRoughness(roughness);

                        ImGui::Text("Albedo Texture: %s",
                            mat->HasAlbedoTexture() ? "Yes" : "None");
                        if (mat->HasNormalTexture())
                            ImGui::Text("Normal Texture: Yes");

                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }
        }

        ImGui::End();
    }
} // namespace Nightbloom