// Panels/LiveShaderTestPanel.cpp
#include "LiveShaderTestPanel.hpp"
#include "Engine/Core/Scene.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanTexture.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <imgui.h>

namespace Nightbloom
{
    void LiveShaderTestPanel::Update(float deltaTime)
    {
        m_Rotation += deltaTime * m_RotationSpeed;
    }

    void LiveShaderTestPanel::Draw(EditorContext& ctx)
    {
        ImGui::Begin("Live Shader Test", &isOpen);

        if (!ctx.scene)
        {
            ImGui::Text("No scene");
            ImGui::End();
            return;
        }

        SceneObject* selected = ctx.scene->GetSelected();
        bool hasPrimitive = selected && selected->meshDrawable;

        if (!hasPrimitive)
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Select a primitive in the hierarchy to test shaders");
            ImGui::Text("(Models use their own materials)");
            ImGui::End();
            return;
        }

        ImGui::Text("Testing on: %s", selected->name.c_str());
        ImGui::Separator();

        // Pipeline selector
        const char* pipelineNames[] = {
            "Triangle", "Mesh", "Transparent", "Shadow", "Skybox",
            "Volumetric", "PostProcess", "Compute", "NodeGenerated"
        };

        int currentPipeline = (int)(selected->pipeline);
        if (ImGui::Combo("Pipeline", &currentPipeline, pipelineNames,
            static_cast<int>(PipelineType::Count)))
        {
            PipelineType newPipeline = static_cast<PipelineType>(currentPipeline);
            auto* pm = ctx.renderer ? ctx.renderer->GetPipelineManager() : nullptr;

            if (pm && pm->GetPipeline(newPipeline) == nullptr)
            {
                LOG_WARN("Pipeline {} does not exist!", pipelineNames[currentPipeline]);
            }
            else
            {
                selected->pipeline = newPipeline;
                ApplyPipelineToSelected(ctx, newPipeline);
                LOG_INFO("Switched to {} pipeline", pipelineNames[currentPipeline]);
            }
        }

        ImGui::Separator();

        const char* textureNames[] = { "UV Checker", "White", "Black", "Normal" };
        const char* textureLookup[] = {
            "uv_checker", "default_white", "default_black", "default_normal"
        };

        if (ImGui::Combo("Texture", &selected->textureIndex, textureNames, 4))
        {
            ResourceManager* resources =
                ctx.renderer ? ctx.renderer->GetResourceManager() : nullptr;

            if (resources && selected->meshDrawable)
            {
                selected->meshDrawable->ClearTextures();
                VulkanTexture* texture =
                    resources->GetTexture(textureLookup[selected->textureIndex]);
                if (texture)
                {
                    selected->meshDrawable->AddTexture(texture);
                    LOG_INFO("Switched to {} texture", textureNames[selected->textureIndex]);
                }
            }
        }

        ImGui::Separator();
        ImGui::SliderFloat("Rotation Speed", &m_RotationSpeed, 0.0f, 5.0f);

        if (ImGui::Button("Reset Rotation"))
        {
            m_Rotation = 0.0f;
            m_RotationSpeed = 1.0f;
        }

        ImGui::End();
    }
} // namespace Nightbloom