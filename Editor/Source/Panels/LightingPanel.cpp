// Panels/LightingPanel.cpp
#include "LightingPanel.hpp"
#include "Engine/Core/Scene.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Light.hpp"
#include <imgui.h>
#include <glm/glm.hpp>

namespace Nightbloom
{
    void LightingPanel::Draw(EditorContext& ctx)
    {
        ImGui::Begin("Lighting", &isOpen);

        if (!ctx.scene)
        {
            ImGui::Text("No scene");
            ImGui::End();
            return;
        }

        Light* selectedLight = ctx.scene->GetSelectedLight();

        if (!selectedLight)
        {
            ImGui::Text("Lights: %zu", ctx.scene->GetLightCount());
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "Select a light in the hierarchy to edit");
            ImGui::End();
            return;
        }

        // Name
        char nameBuf[256];
        strncpy(nameBuf, selectedLight->name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            selectedLight->name = nameBuf;

        ImGui::Checkbox("Enabled", &selectedLight->enabled);

        // Type
        const char* typeNames[] = { "Directional", "Point" };
        int currentType = static_cast<int>(selectedLight->type);
        if (ImGui::Combo("Type", &currentType, typeNames, 2))
            selectedLight->type = static_cast<LightType>(currentType);

        ImGui::Separator();
        ImGui::ColorEdit3("Color", &selectedLight->color.x);
        ImGui::SliderFloat("Intensity", &selectedLight->intensity, 0.0f, 10.0f, "%.2f");
        ImGui::Separator();

        // Type-specific
        if (selectedLight->type == LightType::Directional)
        {
            ImGui::Text("Direction");
            ImGui::DragFloat3("Dir", &selectedLight->direction.x, 0.01f, -1.0f, 1.0f);

            if (ImGui::Button("Normalize Direction"))
            {
                float len = glm::length(selectedLight->direction);
                if (len > 0.0001f)
                    selectedLight->direction = glm::normalize(selectedLight->direction);
            }

            glm::vec3 dir = glm::normalize(selectedLight->direction);
            float elevation = glm::degrees(asinf(-dir.y));
            float azimuth = glm::degrees(atan2f(dir.x, dir.z));
            ImGui::Text("Elevation: %.1f deg, Azimuth: %.1f deg", elevation, azimuth);
        }
        else if (selectedLight->type == LightType::Point)
        {
            ImGui::DragFloat3("Position", &selectedLight->position.x, 0.1f);
            ImGui::SliderFloat("Radius", &selectedLight->radius, 1.0f, 200.0f);

            if (ImGui::CollapsingHeader("Attenuation"))
            {
                ImGui::SliderFloat("Constant", &selectedLight->constant, 0.0f, 5.0f);
                ImGui::SliderFloat("Linear", &selectedLight->linear, 0.0f, 1.0f, "%.4f");
                ImGui::SliderFloat("Quadratic", &selectedLight->quadratic, 0.0f, 0.5f, "%.5f");

                if (ImGui::Button("Short Range"))
                {
                    selectedLight->constant = 1.0f; selectedLight->linear = 0.35f;
                    selectedLight->quadratic = 0.44f; selectedLight->radius = 10.0f;
                }
                ImGui::SameLine();
                if (ImGui::Button("Medium Range"))
                {
                    selectedLight->constant = 1.0f; selectedLight->linear = 0.09f;
                    selectedLight->quadratic = 0.032f; selectedLight->radius = 50.0f;
                }
                ImGui::SameLine();
                if (ImGui::Button("Long Range"))
                {
                    selectedLight->constant = 1.0f; selectedLight->linear = 0.022f;
                    selectedLight->quadratic = 0.0019f; selectedLight->radius = 150.0f;
                }
            }
        }

        // Scene ambient
        ImGui::Separator();
        ImGui::Text("Scene Ambient");

        glm::vec3 ambientColor = ctx.scene->GetAmbientColor();
        float     ambientIntensity = ctx.scene->GetAmbientIntensity();

        if (ImGui::ColorEdit3("Ambient Color", &ambientColor.x))
            ctx.scene->SetAmbient(ambientColor, ambientIntensity);

        if (ImGui::SliderFloat("Ambient Intensity", &ambientIntensity, 0.0f, 2.0f))
            ctx.scene->SetAmbient(ambientColor, ambientIntensity);

        // Shadow settings
        if (ctx.renderer)
        {
            ImGui::Separator();
            ImGui::Text("Shadow Settings");

            bool shadowEnabled = ctx.renderer->IsShadowEnabled();
            if (ImGui::Checkbox("Enable Shadows", &shadowEnabled))
                ctx.renderer->SetShadowEnabled(shadowEnabled);

            static glm::vec3 shadowCenter = glm::vec3(0.0f);
            if (ImGui::DragFloat3("Shadow Center", &shadowCenter.x, 0.5f))
                ctx.renderer->SetShadowCenter(shadowCenter);
        }

        ImGui::End();
    }
} // namespace Nightbloom