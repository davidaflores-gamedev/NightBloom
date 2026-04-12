// Panels/NoiseDebugPanel.cpp
#include "NoiseDebugPanel.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/NoiseTextureGenerator.hpp"
#include "Engine/Renderer/Vulkan/VulkanTexture.hpp"
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

namespace Nightbloom
{
    void NoiseDebugPanel::UnregisterPreview()
    {
        if (m_PreviewID)
        {
            ImGui_ImplVulkan_RemoveTexture(
                reinterpret_cast<VkDescriptorSet>(m_PreviewID));
            m_PreviewID = 0;
            m_RegisteredTex = nullptr;
        }
    }

    void NoiseDebugPanel::Cleanup()
    {
        UnregisterPreview();
    }

    void NoiseDebugPanel::Draw(EditorContext& ctx)
    {
        ImGui::Begin("Noise Debug", &isOpen);

        if (!ctx.renderer)
        {
            ImGui::Text("No renderer");
            ImGui::End();
            return;
        }

        // Parameters
        const char* noiseTypes[] = { "Perlin", "Worley", "PerlinWorley" };
        ImGui::Combo("Type", &m_NoiseType, noiseTypes, 3);
        ImGui::SliderInt("Octaves", &m_NoiseOctaves, 1, 8);
        ImGui::SliderFloat("Frequency", &m_NoiseFreq, 0.5f, 16.0f);
        ImGui::SliderFloat("Persistence", &m_NoisePersist, 0.1f, 1.0f);
        ImGui::SliderFloat("Lacunarity", &m_NoiseLacun, 1.0f, 4.0f);
        ImGui::SliderInt("Seed", &m_NoiseSeed, 0, 999);

        if (ImGui::Button("Generate"))
        {
            NoiseTextureDesc desc;
            desc.width = 256;
            desc.height = 256;
            desc.depth = 1;
            desc.noiseType = static_cast<NoiseType>(m_NoiseType);
            desc.octaves = static_cast<uint32_t>(m_NoiseOctaves);
            desc.frequency = m_NoiseFreq;
            desc.persistence = m_NoisePersist;
            desc.lacunarity = m_NoiseLacun;
            desc.seed = static_cast<uint32_t>(m_NoiseSeed);
            desc.debugName = "NoisePreview";

            // Unregister old before regenerating
            UnregisterPreview();

            ctx.renderer->RegenerateNoisePreview(desc);
        }

        ImGui::Separator();

        // Texture preview
        VulkanTexture* noiseTex = ctx.renderer->GetNoisePreview();
        if (noiseTex)
        {
            // Register with ImGui if new or changed
            if (noiseTex != m_RegisteredTex)
            {
                UnregisterPreview();

                m_PreviewID = reinterpret_cast<ImTextureID>(
                    ImGui_ImplVulkan_AddTexture(
                        noiseTex->GetSampler(),
                        noiseTex->GetImageView(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

                m_RegisteredTex = noiseTex;
            }

            if (m_PreviewID)
            {
                float panelWidth = ImGui::GetContentRegionAvail().x;
                ImGui::Image(m_PreviewID, ImVec2(panelWidth, panelWidth));
                ImGui::Text("256x256 | Type: %s | Octaves: %d | Freq: %.1f",
                    noiseTypes[m_NoiseType], m_NoiseOctaves, m_NoiseFreq);
            }
        }
        else
        {
            ImGui::TextDisabled("No noise texture generated. Press Generate.");
        }

        ImGui::End();
    }
} // namespace Nightbloom