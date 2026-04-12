// Panels/ProjectSettingsPanel.cpp
#include "ProjectSettingsPanel.hpp"
#include "../EditorFileUtils.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <imgui.h>

namespace Nightbloom
{
    void ProjectSettingsPanel::Draw(EditorContext& ctx)
    {
        ImGui::Begin("Project Settings", &isOpen);

        if (ctx.projectName)
            ImGui::Text("Current Project: %s", ctx.projectName->c_str());
        if (ctx.projectPath)
            ImGui::Text("Project Path: %s", ctx.projectPath->string().c_str());

        ImGui::Separator();

        static int buildConfig = 0;
        const char* configs[] = { "Debug", "Release", "RelWithDebInfo" };
        if (ImGui::Combo("Build Configuration", &buildConfig, configs, 3))
        {
            Editor::EditorFileUtils::ProjectContext pctx =
                Editor::EditorFileUtils::GetProjectContext();
            pctx.config = configs[buildConfig];
            Editor::EditorFileUtils::SetProjectContext(pctx);
            LOG_INFO("Changed build configuration to: {}", configs[buildConfig]);
        }

        ImGui::Separator();
        ImGui::Text("Shader Settings");
        ImGui::Text("Source:   %s",
            Editor::EditorFileUtils::GetEditorShadersSourceDirectory().c_str());
        ImGui::Text("Compiled: %s",
            Editor::EditorFileUtils::GetEditorShadersCompiledDirectory().c_str());
        ImGui::Text("Deploy to: %s",
            Editor::EditorFileUtils::GetCurrentProjectShadersDirectory().c_str());

        ImGui::End();
    }
} // namespace Nightbloom