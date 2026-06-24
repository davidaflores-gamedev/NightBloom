// Panels/FireflyPanel.hpp
#pragma once
#include "../EditorContext.hpp"
#include "Engine/VFX/FireflySystem.hpp"
#include <imgui.h>

namespace Nightbloom
{
    class FireflyPanel
    {
    public:
        bool isOpen = true;

        void Draw(EditorContext& ctx);

        // Call before renderer shuts down (while Vulkan device is still alive)
        void Cleanup();

        void SubmitFireflyDraw(DrawList& drawList) const
        {
            if (m_Initialized && m_Firefly.IsReady())
                m_Firefly.SubmitDraw(drawList);
        }

        FireflySystem& GetSystem() { return m_Firefly; }

    private:
        FireflySystem m_Firefly;
        bool          m_Initialized = false;

        // Agent count is fixed at init (buffer size), not live-resizable —
        // changing it requires the explicit "Reinitialize" action below.
        int m_AgentCount = 1500;

        float m_Center[3] = { 0.0f, 10.0f, 0.0f };
        float m_Extents[3] = { 50.0f, 20.0f, 50.0f };

        bool EnsureInitialized(Renderer* renderer);
    };

} // namespace Nightbloom
