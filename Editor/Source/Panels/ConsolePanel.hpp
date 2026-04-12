// Panels/ConsolePanel.hpp
#pragma once
#include "../EditorContext.hpp"

namespace Nightbloom
{
    class ConsolePanel
    {
    public:
        bool isOpen = true;
        void Draw(EditorContext& ctx);
    };
} // namespace Nightbloom