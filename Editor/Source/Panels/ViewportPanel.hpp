// Panels/ViewportPanel.hpp
#pragma once
#include "../EditorContext.hpp"

namespace Nightbloom
{
    class ViewportPanel
    {
    public:
        bool isOpen = true;
        bool isPlayMode = false;  // Owned here, menu bar/EditorApp can read it
        void Draw(EditorContext& ctx);
    };
} // namespace Nightbloom