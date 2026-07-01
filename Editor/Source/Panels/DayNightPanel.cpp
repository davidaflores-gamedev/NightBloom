// Panels/DayNightPanel.cpp
#include "DayNightPanel.hpp"
#include "Engine/Core/Scene.hpp"
#include "Engine/Renderer/Light.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace Nightbloom
{
    void DayNightPanel::Apply(Scene& scene, float deltaTime)
    {
        if (enabled && autoAdvance)
        {
            timeOfDay += deltaTime * speedHours;
            timeOfDay = std::fmod(timeOfDay, 24.0f);
            if (timeOfDay < 0.0f) timeOfDay += 24.0f;
        }

        // Sun direction (from origin toward the sun). The arc runs midnight (below)
        // -> 6h east horizon -> noon (top) -> 18h west horizon, with arcTilt spreading
        // it off the pure X-Y plane so shadows aren't dead-on. sunPos.y is the signed
        // elevation: positive = daytime, negative = the sun is down (night).
        const float PI = 3.14159265358979323846f;
        float t   = timeOfDay / 24.0f;
        float ang = t * 2.0f * PI - PI * 0.5f;
        glm::vec3 sunPos = glm::normalize(glm::vec3(
            std::cos(ang) * std::cos(arcTilt),
            std::sin(ang),
            std::cos(ang) * std::sin(arcTilt)));

        // Single-light engine: keep the lit body in the UPPER hemisphere always, so
        // the scene is lit from above at all hours (sun by day, its mirror = the moon
        // by night). The disc is faded out near the horizon so the sun->moon handoff
        // at dusk/dawn is hidden while the warm horizon glow (below) takes over.
        glm::vec3 bodyUp   = (sunPos.y >= 0.0f) ? sunPos : -sunPos;
        glm::vec3 lightDir = -bodyUp; // light travels from the body down into the scene

        float dayFactor     = glm::smoothstep(-0.05f, 0.30f, sunPos.y);          // 0 night .. 1 day
        float horizonFactor = (1.0f - glm::smoothstep(0.0f, 0.30f, bodyUp.y)) * horizonStrength;
        float discFade      = glm::smoothstep(0.0f, 0.30f, bodyUp.y);            // hide the horizon swap

        Light* light = (scene.GetLightCount() > 0) ? scene.GetLight(0) : nullptr;

        if (enabled && light)
        {
            light->direction = lightDir;

            glm::vec3 lc = glm::mix(nightLightColor, dayLightColor, dayFactor);
            lc = glm::mix(lc, horizonColor, horizonFactor);
            light->color = lc;
            light->intensity = glm::mix(nightLightIntensity, dayLightIntensity, dayFactor);

            glm::vec3 amb = glm::mix(nightAmbientColor, dayAmbientColor, dayFactor);
            float ambI    = glm::mix(nightAmbientIntensity, dayAmbientIntensity, dayFactor);
            scene.SetAmbient(amb, ambI);
        }

        // Position + tint the celestial disc (the "Moon" object). Runs even when the
        // cycle isn't driving the light, so the disc always tracks lights[0].
        glm::vec3 trackDir = light ? glm::normalize(light->direction) : lightDir;
        glm::vec3 discPos  = -trackDir * discDistance;

        for (auto& obj : scene.GetObjects())
        {
            if (obj.name != "Moon" || !obj.meshDrawable) continue;

            glm::mat4 xform =
                glm::translate(glm::mat4(1.0f), discPos) *
                glm::scale(glm::mat4(1.0f), glm::vec3(discRadius));
            obj.meshDrawable->SetTransform(xform);
            obj.primitiveTransform = xform;

            if (enabled)
            {
                glm::vec3 dc = glm::mix(nightDiscColor, dayDiscColor, dayFactor);
                dc = glm::mix(dc, horizonColor, horizonFactor);
                float di = glm::mix(nightDiscIntensity, dayDiscIntensity, dayFactor) * discFade;
                // rgb = HDR emissive color (tint * intensity); w flags emissive AND body type
                // for Mesh.frag: 3.0 = sun (flat/hot), 2.0 = moon (limb-darkened + craters).
                // The sun<->moon swap happens at the horizon where discFade ~ 0 (disc hidden),
                // so the appearance change is never visible as a pop.
                float discFlag = (sunPos.y >= 0.0f) ? 3.0f : 2.0f;
                obj.meshDrawable->SetCustomData(glm::vec4(dc * di, discFlag));
                // Hide entirely once faded out, else a zero-color emissive disc would
                // still write depth as a black spot on the horizon.
                obj.visible = (discFade > 0.02f);
            }
            break;
        }
    }

    void DayNightPanel::Draw(EditorContext& ctx)
    {
        (void)ctx; // panel edits its own state; EditorApp applies it in OnUpdate
        if (!isOpen) return;
        if (!ImGui::Begin("Day / Night", &isOpen)) { ImGui::End(); return; }

        ImGui::Checkbox("Drive lighting (enable cycle)", &enabled);

        ImGui::SliderFloat("Time of Day", &timeOfDay, 0.0f, 24.0f, "%.2f h");
        int hh = static_cast<int>(timeOfDay);
        int mm = static_cast<int>((timeOfDay - hh) * 60.0f);
        ImGui::SameLine();
        ImGui::Text("(%02d:%02d)", hh, mm);

        ImGui::Checkbox("Auto-advance", &autoAdvance);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("Speed (h/s)", &speedHours, 0.1f, 24.0f, "%.1f");
        ImGui::SliderFloat("Arc tilt", &arcTilt, -1.0f, 1.0f, "%.2f rad");

        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::ColorEdit3("Day color", &dayLightColor.x);
            ImGui::ColorEdit3("Night color", &nightLightColor.x);
            ImGui::ColorEdit3("Horizon (dawn/dusk)", &horizonColor.x);
            ImGui::SliderFloat("Horizon strength", &horizonStrength, 0.0f, 1.0f);
            ImGui::SliderFloat("Day intensity", &dayLightIntensity, 0.0f, 5.0f);
            ImGui::SliderFloat("Night intensity", &nightLightIntensity, 0.0f, 3.0f);
        }

        if (ImGui::CollapsingHeader("Ambient"))
        {
            ImGui::ColorEdit3("Day ambient", &dayAmbientColor.x);
            ImGui::ColorEdit3("Night ambient", &nightAmbientColor.x);
            ImGui::SliderFloat("Day ambient intensity", &dayAmbientIntensity, 0.0f, 3.0f);
            ImGui::SliderFloat("Night ambient intensity", &nightAmbientIntensity, 0.0f, 3.0f);
        }

        if (ImGui::CollapsingHeader("Moon / Sun Disc", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::ColorEdit3("Day disc color", &dayDiscColor.x);
            ImGui::ColorEdit3("Night disc color", &nightDiscColor.x);
            ImGui::SliderFloat("Day disc intensity", &dayDiscIntensity, 0.0f, 20.0f);
            ImGui::SliderFloat("Night disc intensity", &nightDiscIntensity, 0.0f, 20.0f);
            ImGui::SliderFloat("Disc distance", &discDistance, 50.0f, 1000.0f);
            ImGui::SliderFloat("Disc radius", &discRadius, 5.0f, 200.0f);
        }

        ImGui::End();
    }
} // namespace Nightbloom
