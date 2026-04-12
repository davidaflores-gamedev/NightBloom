// Panels/DebugPanel.hpp
#pragma once
#include "../EditorContext.hpp"

namespace Nightbloom
{
    class DebugPanel
    {
    public:
        bool isOpen = false;
        void Draw(EditorContext& ctx);

    private:
        bool m_ComputeTestRan = false;
    };
} // namespace Nightbloom