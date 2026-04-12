// Panels/ProjectSettingsPanel.hpp
#pragma once
#include "../EditorContext.hpp"

namespace Nightbloom
{
    class ProjectSettingsPanel
    {
    public:
        bool isOpen = false;
        void Draw(EditorContext& ctx);
    };
} // namespace Nightbloom