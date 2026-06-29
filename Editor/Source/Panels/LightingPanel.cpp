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
            // Cascades (frustum-fit CSM). Ortho Size / Near / Far no longer apply:
            // each cascade is auto-fit to a slice of the camera view frustum.
            // -----------------------------------------------------------------
            ImGui::Spacing();
            ImGui::TextUnformatted("Cascades");
            ImGui::Separator();

            if (ImGui::SliderFloat("Shadow Distance", &cfg.shadowDistance, 20.0f, 500.0f, "%.0f"))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Max view distance shadows are fit to (outermost cascade far plane).\n"
                    "Smaller = cascades pack closer = crisper, but shadows end nearer.");

            if (ImGui::SliderFloat("Split Distribution", &cfg.splitLambda, 0.0f, 1.0f, "%.2f"))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Cascade split blend: 0 = uniform slices, 1 = logarithmic.\n"
                    "Higher shrinks the near cascades -> much sharper shadows up close.\n"
                    "*** This is the main knob for close-up shadow quality. ***");

            if (ImGui::SliderFloat("Caster Extrude", &cfg.casterExtrude, 0.0f, 200.0f, "%.0f"))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Extra depth pulled toward the light so tall occluders just outside a\n"
                    "cascade still cast into it. Raise if edge shadows pop in/out.");

            if (ImGui::SliderFloat("Cascade Blend", &cfg.cascadeBlend, 0.0f, 0.5f, "%.2f"))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Cross-fade width between cascades (fraction of each cascade's depth).\n"
                    "0 = hard cuts (visible seam where an object spans two cascades).\n"
                    "Raise until the half-and-half shadow split disappears.");

            // Shadow map resolution — the direct lever for far-cascade quality. Not part of
            // ShadowConfig (it's a GPU resource size), so applied immediately, not via `changed`.
            {
                static const int   kResValues[] = { 1024, 2048, 4096 };
                static const char* kResLabels[] = { "1024 (12 MB)", "2048 (48 MB)", "4096 (192 MB)" };
                uint32_t curRes = ctx.renderer->GetShadowResolution();
                int resIdx = (curRes <= 1024) ? 0 : (curRes >= 4096) ? 2 : 1;
                if (ImGui::Combo("Shadow Resolution", &resIdx, kResLabels, 3))
                    ctx.renderer->SetShadowResolution(static_cast<uint32_t>(kResValues[resIdx]));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Texels per cascade layer. The most direct fix for a blurry far\n"
                        "cascade at long Shadow Distance. Memory shown is for all cascades.");
            }

            // -----------------------------------------------------------------
            // Bias
            // -----------------------------------------------------------------
            ImGui::Spacing();
            ImGui::TextUnformatted("Bias");
            ImGui::Separator();

            // Logarithmic sliders: fine control near zero AND full reach to the max
            // in one drag (bias values span orders of magnitude). Ctrl+Click to type.
            if (ImGui::SliderFloat("Bias##shadow",
                &cfg.bias, 0.0f, 0.01f, "%.5f",
                ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Constant depth offset applied during the shadow map lookup.\n"
                    "Too low  -> shadow acne (self-shadowing speckles on lit surfaces).\n"
                    "Too high -> Peter Panning (shadow detaches from caster).\n\n"
                    "Ctrl+Click to type an exact value.");

            if (ImGui::SliderFloat("Normal Bias",
                &cfg.normalBias, 0.0f, 0.1f, "%.4f",
                ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
                changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Offset along the surface normal before the shadow lookup.\n"
                    "Fixes wrong-side shadowing at shadow terminator edges.\n"
                    "Too high -> shadow creeps away from the caster along edges.");

            // -----------------------------------------------------------------
            // Presets
            // -----------------------------------------------------------------
            ImGui::Spacing();

            if (ImGui::Button("Reset to Scene Defaults"))
            {
                cfg.shadowDistance = 200.0f;
                cfg.splitLambda    = 0.85f;
                cfg.casterExtrude  = 50.0f;
                cfg.cascadeBlend   = 0.25f;
                cfg.bias = 0.0015f;
                cfg.normalBias = 0.03f;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Crisp (near detail)"))
            {
                cfg.shadowDistance = 100.0f;
                cfg.splitLambda    = 0.95f;
                cfg.casterExtrude  = 40.0f;
                cfg.cascadeBlend   = 0.30f;
                cfg.bias = 0.0015f;
                cfg.normalBias = 0.03f;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Packs cascades close for maximum sharpness near the camera.");

            ImGui::SameLine();
            if (ImGui::Button("Far (big landscape)"))
            {
                cfg.shadowDistance = 400.0f;
                cfg.splitLambda    = 0.80f;
                cfg.casterExtrude  = 80.0f;
                cfg.cascadeBlend   = 0.20f;
                cfg.bias = 0.003f;
                cfg.normalBias = 0.02f;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Long shadow reach for terrain; softer near the camera.");

            // Push to renderer on any change
            if (changed)
                ctx.renderer->SetShadowConfig(cfg);

            // -----------------------------------------------------------------
            // CSM debug
            // -----------------------------------------------------------------
            ImGui::Spacing();
            bool cascadeTint = ctx.renderer->GetDebugCascadeTint();
            if (ImGui::Checkbox("Debug Cascades", &cascadeTint))
                ctx.renderer->SetDebugCascadeTint(cascadeTint);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Tint surfaces by which shadow cascade they sample.\n"
                    "Cascade 0 = red (nearest, sharpest), 1 = green, 2 = blue (farthest).\n"
                    "The colored rings should track the camera as you move.");

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
                ImGui::TextDisabled("Coverage (4096 shadow map):");

                float texelsPerUnit = 4096.0f / (live.orthoSize * 2.0f);
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