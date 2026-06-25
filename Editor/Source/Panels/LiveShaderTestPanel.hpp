// Panels/LiveShaderTestPanel.hpp
#pragma once
#include "../EditorContext.hpp"
#include "../EditorUtils.hpp"

namespace Nightbloom
{
    class LiveShaderTestPanel
    {
    public:
        bool isOpen = false;

        void Update(float deltaTime);   // Call from EditorApp::OnUpdate
        void Draw(EditorContext& ctx);

        float GetRotation() const { return m_Rotation; }

    private:
        float m_Rotation = 0.0f;
        float m_RotationSpeed = 1.0f;
    };
} // namespace Nightbloom