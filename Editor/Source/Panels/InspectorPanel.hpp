// Panels/InspectorPanel.hpp
#pragma once
#include "../EditorContext.hpp"

namespace Nightbloom
{
    class InspectorPanel
    {
    public:
        bool isOpen = true;
        void Draw(EditorContext& ctx);
    };
} // namespace Nightbloom