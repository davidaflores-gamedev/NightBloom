// Panels/AssetBrowserPanel.hpp
#pragma once
#include "../EditorContext.hpp"

namespace Nightbloom
{
    class AssetBrowserPanel
    {
    public:
        bool isOpen = false;
        void Draw(EditorContext& ctx);
    };
} // namespace Nightbloom