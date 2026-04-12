// Panels/LightingPanel.hpp
#pragma once
#include "../EditorContext.hpp"

namespace Nightbloom
{
    class LightingPanel
    {
    public:
        bool isOpen = true;
        void Draw(EditorContext& ctx);
    };
} // namespace Nightbloom