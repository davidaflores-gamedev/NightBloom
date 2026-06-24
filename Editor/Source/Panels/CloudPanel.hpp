// Panels/CloudPanel.hpp
#pragma once
#include "../EditorContext.hpp"
#include "Engine/VFX/CloudSystem.hpp"
#include <imgui.h>

namespace Nightbloom
{
    class CloudPanel
    {
    public:
        bool isOpen = true;

        void Draw(EditorContext& ctx);

        // Call before renderer shuts down (while Vulkan device is still alive)
        void Cleanup();

        void SubmitCloudDraw(DrawList& drawList) const
        {
            if (m_Initialized && m_Clouds.IsReady())
                m_Clouds.SubmitDraw(drawList);
        }

        CloudSystem& GetSystem() { return m_Clouds; }

    private:
        CloudSystem m_Clouds;
        bool        m_Initialized = false;
        bool        m_PendingDirty = false;

        // Noise texture settings — only these require "Regenerate Noise" to
        // take effect (rebuilding the shape/detail textures). Everything
        // else (layer bounds, wind, scale, coverage, density, step count...)
        // lives directly on CloudSystem::GetDesc() and is tunable live —
        // sliders bind straight to that struct, same pattern FireflyPanel
        // uses for its params.
        int   m_ShapeResIndex = 1;   // 0=64 1=128 2=256
        int   m_ShapeOctaves = 5;
        float m_ShapeFrequency = 4.0f;
        int   m_DetailResIndex = 0;  // 0=32 1=64
        int   m_DetailOctaves = 3;
        float m_DetailFrequency = 8.0f;
        int   m_Seed = 1337;

        CloudDesc BuildDesc() const;
        bool      EnsureInitialized(Renderer* renderer);
    };

} // namespace Nightbloom
