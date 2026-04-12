// Panels/SceneHierarchyPanel.hpp
#pragma once
#include "../EditorContext.hpp"

namespace Nightbloom
{
    class SceneHierarchyPanel
    {
    public:
        bool isOpen = true;
        void Draw(EditorContext& ctx);
    };
} // namespace Nightbloom