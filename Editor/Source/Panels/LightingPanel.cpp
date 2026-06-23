// Panels/LightingPanel.cpp
#include "LightingPanel.hpp"
#include "Engine/Core/Scene.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Light.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <cmath>

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

        // -------------------------------------------------------------------------
        // Identity
        // -------------------------------------------------------------------------
        char nameBuf[256];
        strncpy(nameBuf, selectedLight->name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            selectedLight->name = nameBuf;

        ImGui::Checkbox("Enabled", &selectedLight->enabled);

        const char* typeNames[] = { "Directional", "Point" };
        int currentType = static_cast<int>(selectedLight->type);
        if (ImGui::Combo("Type", &currentType, typeNames, 2))
            selectedLight->type = static_cast<LightType>(currentType);

        // -------------------------------------------------------------------------
        // Color / intensity
        // -------------------------------------------------------------------------
        ImGui::Separator();
        ImGui::ColorEdit3("Color", &selectedLight->color.x);
        ImGui::SliderFloat("Intensity", &selectedLight->intensity, 0.0f, 10.0f, "%.2f");

        // -------------------------------------------------------------------------
        // Type-specific properties
        // -------------------------------------------------------------------------
        ImGui::Separator();

        if (selectedLight->type == LightType::Directional)
        {
            ImGui::Text("Direction");
            ImGui::DragFloat3("Dir", &selectedLight->direction.x, 0.01f, -1.0f, 1.0f);

            if (ImGui::Button("Normalize"))
            {
                float len = glm::length(selectedLight->direction);
                if (len > 0.0001f)
                    selectedLight->direction = glm::normalize(selectedLight->direction);
            }

            glm::vec3 dir = glm::normalize(selectedLight->direction);
            float elevation = glm::degrees(asinf(-dir.y));
            float azimuth = glm::degrees(atan2f(dir.x, dir.z));
            ImGui::TextDisabled("Elevation: %.1f deg   Azimuth: %.1f deg", elevation, azimuth);
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
                    selectedLight->constant = 1.0f;  selectedLight->linear = 0.35f;
                    selectedLight->quadratic = 0.44f; selectedLight->radius = 10.0f;
                }
                ImGui::SameLine();
                if (ImGui::Button("Medium Range"))
                {
                    selectedLight->constant = 1.0f;  selectedLight->linear = 0.09f;
                    selectedLight->quadratic = 0.032f; selectedLight->radius = 50.0f;
                }
                ImGui::SameLine();
                if (ImGui::Button("Long Range"))
                {
                    selectedLight->constant = 1.0f;  selectedLight->linear = 0.022f;
                    selectedLight->quadratic = 0.0019f; selectedLight->radius = 150.0f;
                }
            }
        }

        // -------------------------------------------------------------------------
        // Scene ambient
        // -------------------------------------------------------------------------
        ImGui::Separator();
        ImGui::Text("Scene Ambient");

        glm::vec3 ambientColor = ctx.scene->GetAmbientColor();
        float     ambientIntensity = ctx.scene->GetAmbientIntensity();

        if (ImGui::ColorEdit3("Ambient Color", &ambientColor.x))
            ctx.scene->SetAmbient(ambientColor, ambientIntensity);
        if (ImGui::SliderFloat("Ambient Intensity", &ambientIntensity, 0.0f, 2.0f))
            ctx.scene->SetAmbient(ambientColor, ambientIntensity);

        // -------------------------------------------------------------------------
        // Shadow mapping — only meaningful for directional lights
        // -------------------------------------------------------------------------
        if (!ctx.renderer) { ImGui::End(); return; }

        ImGui::Separator();

        bool shadowSectionOpen = ImGui::CollapsingHeader(
            "Shadow Mapping", ImGuiTreeNodeFlags_DefaultOpen);

        if (shadowSectionOpen)
        {
            // Global enable — gates the entire shadow pass
            bool shadowEnabled = ctx.renderer->IsShadowEnabled();
            if (ImGui::Checkbox("Enable Shadow Pass", &shadowEnabled))
                ctx.renderer->SetShadowEnabled(shadowEnabled);

            if (selectedLight->type != LightType::Directional)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                    "Shadow mapping is only supported for Directional lights.");
                ImGui::End();
                return;
            }

            ImGui::Spacing();

            ShadowConfig& cfg = selectedLight->shadowConfig;
            bool changed = false;

            // Whether this specific light casts shadows
            if (ImGui::Checkbox("Light Casts Shadows", &cfg.castsShadows))
                changed = true;

            if (!cfg.castsShadows)
            {
                ImGui::TextDisabled("Enable 'Light Casts Shadows' to configure.");
                if (changed) ctx.renderer->SetShadowConfig(cfg);
                ImGui::End();
                return;
            }

            // -----------------------------------------------------------------
            // Frustum
            // -----------------------------------------------------------------
            ImGui::Spacing();
            ImGui::TextUnformatted("Frustum");
            ImGui::Separator();

            if (ImGui::SliderFloat("Ortho Size", &cfg.orthoSize, 0.5f, 200.0f, "%.2f"))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Half-extent of the orthographic shadow frustum.\n"
                    "Smaller = more texel density on nearby geometry.\n"
                    "Larger  = more scene coverage but softer / blockier shadows.\n"
                    "Match to the rough radius of the content you want shadowed.");

            if (ImGui::SliderFloat("Near Plane", &cfg.nearPlane, 0.001f, 50.0f, "%.3f"))
                changed = true;
            if (ImGui::SliderFloat("Far Plane", &cfg.farPlane, 1.0f, 500.0f, "%.1f"))
                changed = true;

            // Guard against degenerate frustum
            if (cfg.nearPlane >= cfg.farPlane)
                cfg.nearPlane = cfg.farPlane * 0.01f;

            // -----------------------------------------------------------------
            // Bias
            // -----------------------------------------------------------------
            ImGui::Spacing();
            ImGui::TextUnformatted("Bias");
            ImGui::Separator();

            // DragFloat lets you reach very small values without fighting a slider range.
            // Ctrl+Click to type an exact value.
            if (ImGui::DragFloat("Bias##shadow",
                &cfg.bias, 0.000001f, 0.0f, 0.01f, "%.7f",
                ImGuiSliderFlags_AlwaysClamp))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Constant depth offset applied during the shadow map lookup.\n"
                    "Too low  -> shadow acne (self-shadowing speckles on lit surfaces).\n"
                    "Too high -> Peter Panning (shadow detaches from caster).\n\n"
                    "Ctrl+Click to type an exact value.");

            if (ImGui::DragFloat("Normal Bias",
                &cfg.normalBias, 0.000001f, 0.0f, 0.1f, "%.7f",
                ImGuiSliderFlags_AlwaysClamp))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Offset along the surface normal before the shadow lookup.\n"
                    "Fixes wrong-side shadowing at shadow terminator edges.\n"
                    "Too high -> shadow creeps away from the caster along edges.");

            // -----------------------------------------------------------------
            // Frustum center
            // -----------------------------------------------------------------
            ImGui::Spacing();
            ImGui::TextUnformatted("Frustum Center");
            ImGui::Separator();

            if (ImGui::DragFloat3("Center##shadowCenter",
                &m_ShadowCenter.x, 0.01f, -200.0f, 200.0f, "%.2f"))
                ctx.renderer->SetShadowCenter(m_ShadowCenter);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "World-space point the shadow frustum is centered on.\n"
                    "Aim at your primary geometry for the best coverage.");

            // -----------------------------------------------------------------
            // Presets
            // -----------------------------------------------------------------
            ImGui::Spacing();

            if (ImGui::Button("Reset to Scene Defaults"))
            {
                cfg.orthoSize = 50.0f;
                cfg.nearPlane = 10.0f;
                cfg.farPlane = 70.0f;
                cfg.bias = 0.0006f;
                cfg.normalBias = 0.004f;
                m_ShadowCenter = glm::vec3(0.0f);
                ctx.renderer->SetShadowCenter(m_ShadowCenter);
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Tight (small object)"))
            {
                cfg.orthoSize = 2.0f;
                cfg.nearPlane = 0.01f;
                cfg.farPlane = 10.0f;
                cfg.bias = 0.00001f;
                cfg.normalBias = 0.0001f;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Good starting point for small / close-up geometry.");

            ImGui::SameLine();
            if (ImGui::Button("Wide (terrain)"))
            {
                cfg.orthoSize = 100.0f;
                cfg.nearPlane = 1.0f;
                cfg.farPlane = 300.0f;
                cfg.bias = 0.005f;
                cfg.normalBias = 0.02f;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Good starting point for large terrain / landscape scenes.");

            // Push to renderer on any change
            if (changed)
                ctx.renderer->SetShadowConfig(cfg);

            // -----------------------------------------------------------------
            // Diagnostics
            // -----------------------------------------------------------------
            ImGui::Spacing();

            if (ImGui::CollapsingHeader("Diagnostics##shadow"))
            {
                const ShadowConfig& live = ctx.renderer->GetShadowConfig();

                ImGui::TextDisabled("Live renderer values:");
                ImGui::Text("orthoSize:   %.4f", live.orthoSize);
                ImGui::Text("nearPlane:   %.4f", live.nearPlane);
                ImGui::Text("farPlane:    %.4f", live.farPlane);
                ImGui::Text("bias:        %.8f", live.bias);
                ImGui::Text("normalBias:  %.8f", live.normalBias);

                ImGui::Spacing();
                ImGui::TextDisabled("Coverage (2048 shadow map):");

                float texelsPerUnit = 2048.0f / (live.orthoSize * 2.0f);
                float unitsPerTexel = 1.0f / texelsPerUnit;
                ImGui::Text("%.2f texels / unit", texelsPerUnit);
                ImGui::Text("%.6f units / texel", unitsPerTexel);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Objects smaller than 'units/texel' get <= 1 shadow texel.\n"
                        "Aim for your smallest caster to span 50+ texels.");

                // Visual warning when coverage is clearly too low
                if (texelsPerUnit < 10.0f)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f),
                        "Warning: very low texel density — expect jagged edges.");
                }
                else if (texelsPerUnit < 50.0f)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                        "Low texel density — shadows may look blocky.");
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                        "Texel density looks healthy.");
                }
            }
        }

        ImGui::End();
    }

} // namespace Nightbloom