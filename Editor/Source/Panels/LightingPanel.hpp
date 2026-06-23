// Panels/LightingPanel.hpp
#pragma once
#include "../EditorContext.hpp"
#include <glm/glm.hpp>

namespace Nightbloom
{
    class LightingPanel
    {
    public:
        bool isOpen = true;
        void Draw(EditorContext& ctx);

    private:
        // Persists shadow frustum center across frames without a static local.
        // Promoted from static so the value survives panel re-opens cleanly.
        glm::vec3 m_ShadowCenter = glm::vec3(0.0f);
    };
} // namespace Nightbloom