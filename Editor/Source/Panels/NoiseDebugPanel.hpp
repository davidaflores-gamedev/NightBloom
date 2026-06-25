// Panels/NoiseDebugPanel.hpp
#pragma once
#include "../EditorContext.hpp"
#include <imgui.h>

namespace Nightbloom
{
    class VulkanTexture;

    class NoiseDebugPanel
    {
    public:
        bool isOpen = false;

        void Draw(EditorContext& ctx);

        // Call this before the renderer shuts down, while the Vulkan device is still alive.
        void Cleanup();

    private:
        // Parameters
        int   m_NoiseType = 0;   // 0=Perlin 1=Worley 2=PerlinWorley
        int   m_NoiseOctaves = 4;
        float m_NoiseFreq = 4.0f;
        float m_NoisePersist = 0.5f;
        float m_NoiseLacun = 2.0f;
        int   m_NoiseSeed = 42;

        // ImGui texture state
        ImTextureID    m_PreviewID = 0;
        VulkanTexture* m_RegisteredTex = nullptr;

        void UnregisterPreview();
    };
} // namespace Nightbloom