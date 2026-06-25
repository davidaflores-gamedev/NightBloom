// Panels/WaterEditorPanel.hpp
//
// Owns the WaterSystem (a single reflective water plane) and exposes its
// tunables. Mirrors CloudPanel/TerrainEditorPanel: lazy-initialises on first
// Draw, registers with the Renderer (SetWaterSystem) so the reflection pass
// runs, and Cleanup() must run before the Renderer shuts down.
#pragma once
#include "../EditorContext.hpp"
#include "Engine/Hydro/WaterSystem.hpp"
#include <imgui.h>

namespace Nightbloom
{
    class WaterEditorPanel
    {
    public:
        bool isOpen = true;

        void Draw(EditorContext& ctx);

        // Call before the renderer shuts down (Vulkan device still alive).
        void Cleanup();

        void SubmitWaterDraw(DrawList& drawList) const
        {
            if (m_Initialized && m_Water.IsReady())
                m_Water.SubmitDraw(drawList);
        }

        WaterSystem& GetSystem() { return m_Water; }

    private:
        WaterSystem m_Water;
        bool        m_Initialized = false;

        // Mirror the live desc so resolution/worldSize edits (which need a mesh
        // rebuild) can be detected; everything else binds straight to the live
        // desc and is read per-frame in SubmitDraw / the reflection matrix.
        bool EnsureInitialized(Renderer* renderer);
    };

} // namespace Nightbloom
