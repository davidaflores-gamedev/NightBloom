// Panels/DebugPanel.cpp
#include "DebugPanel.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Components/GpuProfiler.hpp"
#include "Engine/Core/Scene.hpp"
#include <imgui.h>

namespace Nightbloom
{
    void DebugPanel::Draw(EditorContext& ctx)
    {
        ImGui::Begin("Debug Panel", &isOpen);

        ImGui::Text("Compute Pipeline Testing");
        ImGui::Separator();

        if (ImGui::Button("Run Compute Test", ImVec2(200, 30)))
        {
            if (ctx.renderer)
            {
                ctx.renderer->PrintComputeTestResults();
                m_ComputeTestRan = true;
            }
        }

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Runs a simple compute shader that multiplies 64 floats by 2.\nCheck the console/log for results.");

        ImGui::Spacing();
        ImGui::Text("The compute test:");
        ImGui::BulletText("Input:  1, 2, 3, ... 64");
        ImGui::BulletText("Output: 2, 4, 6, ... 128");

        if (m_ComputeTestRan)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                "Test executed - check console for results");
        }

        ImGui::Separator();
        ImGui::Text("Shader Tools");

        if (ImGui::Button("Reload All Shaders", ImVec2(200, 25)))
            if (ctx.renderer) ctx.renderer->ReloadShaders();

        if (ImGui::Button("Toggle Pipeline (P)", ImVec2(200, 25)))
            if (ctx.renderer) ctx.renderer->TogglePipeline();

        ImGui::Separator();
        ImGui::Text("Memory & Performance");
        ImGui::Text("Press F3 to toggle performance overlay");
        ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);

        // Per-pass GPU timings (timestamp queries; ~1 frame-in-flight latency).
        if (ctx.renderer)
        {
            GpuProfiler* prof = ctx.renderer->GetGpuProfiler();
            if (prof && prof->IsSupported())
            {
                ImGui::Spacing();
                ImGui::Text("GPU (per pass):");
                float total = 0.0f;
                for (const auto& r : prof->GetResults())
                {
                    ImGui::Text("  %-16s %6.3f ms", r.name.c_str(), r.ms);
                    total += r.ms;
                }
                if (!prof->GetResults().empty())
                    ImGui::Text("  %-16s %6.3f ms", "GPU total", total);
            }
            else if (prof)
            {
                ImGui::TextDisabled("GPU timestamps unsupported on this device");
            }
        }

        ImGui::Separator();
        ImGui::Text("Post-Process");
        if (ctx.renderer)
        {
            bool aaEnabled = ctx.renderer->IsPostProcessAAEnabled();
            if (ImGui::Checkbox("Anti-Aliasing (FXAA)", &aaEnabled))
                ctx.renderer->SetPostProcessAAEnabled(aaEnabled);
            ImGui::TextDisabled("Toggle to compare edge softening in the same view.");
        }

        ImGui::Separator();
        ImGui::Text("Frustum Culling");
        if (ctx.scene)
        {
            ImGui::Text("Scene objects: %zu / Culled: %zu",
                ctx.scene->GetLastObjectCount(), ctx.scene->GetLastCulledCount());
        }

        ImGui::End();
    }
} // namespace Nightbloom