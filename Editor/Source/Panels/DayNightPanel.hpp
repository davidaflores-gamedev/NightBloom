// Panels/DayNightPanel.hpp
//
// Drives a simple day/night cycle from a single time-of-day parameter. The
// engine only supports one shadow-casting directional light (lights[0]), so a
// single celestial disc (the moon we built for bloom) marks that light and is
// tinted warm/bright by day, cool/dim by night. This panel owns the tunable
// state and renders the sliders; EditorApp calls Apply() each frame (before
// BuildLightingData) to write the result into the Scene + moon object.
#pragma once
#include "../EditorContext.hpp"
#include <glm/glm.hpp>

namespace Nightbloom
{
    class Scene;

    class DayNightPanel
    {
    public:
        bool isOpen = true;

        void Draw(EditorContext& ctx);
        // Advances time (if auto) and writes light dir/color/intensity, ambient,
        // and the moon disc's transform + emissive color into the scene.
        void Apply(Scene& scene, float deltaTime);

        // --- Cycle state ---
        bool  enabled       = true;   // when off, only the moon position tracks the light
        float timeOfDay     = 22.0f;  // hours [0,24)
        bool  autoAdvance   = false;
        float speedHours    = 1.0f;   // game-hours advanced per real second
        float arcTilt       = 0.35f;  // radians; spreads the sun/moon arc off the pure X-Y plane

        // --- Directional light (sun by day / moon by night), blended by elevation ---
        glm::vec3 dayLightColor    = glm::vec3(1.0f, 0.96f, 0.86f);
        glm::vec3 nightLightColor  = glm::vec3(0.60f, 0.70f, 1.00f);
        glm::vec3 horizonColor     = glm::vec3(1.00f, 0.45f, 0.18f); // dawn/dusk warm tint
        float     horizonStrength  = 0.7f;
        float     dayLightIntensity   = 1.1f;
        float     nightLightIntensity = 0.5f;

        // --- Ambient (sky fill), blended by elevation ---
        // Day ambient stays modest — a flat ~0.5 floor washes the whole scene out
        // on top of the HDR pipeline. Effective ambient = color * intensity.
        glm::vec3 dayAmbientColor    = glm::vec3(0.10f, 0.12f, 0.16f);
        glm::vec3 nightAmbientColor  = glm::vec3(0.03f, 0.03f, 0.06f);
        float     dayAmbientIntensity   = 1.0f;
        float     nightAmbientIntensity = 1.0f;

        // --- Celestial disc: sun by day / moon by night ---
        // Pushed to opposite extremes so bloom can't wash them into the same white
        // blob: sun = hot orange, well above the bloom threshold (big warm halo);
        // moon = pale blue, dim enough to sit near/below the threshold so it stays a
        // crisp blue disc with visible maria instead of a glowing white ball.
        glm::vec3 dayDiscColor     = glm::vec3(1.00f, 0.68f, 0.30f);
        glm::vec3 nightDiscColor   = glm::vec3(0.55f, 0.70f, 1.00f);
        float     dayDiscIntensity   = 12.0f;
        float     nightDiscIntensity = 1.1f;
        float     discDistance = 350.0f;
        float     discRadius   = 45.0f;
    };
} // namespace Nightbloom
