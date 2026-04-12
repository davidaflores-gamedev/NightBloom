// Panels/ConsolePanel.cpp
#include "ConsolePanel.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <imgui.h>

namespace Nightbloom
{
    void ConsolePanel::Draw(EditorContext& /*ctx*/)
    {
        ImGui::Begin("Console", &isOpen);

        ImGui::Text("Console output:");
        ImGui::Separator();

        ImGui::BeginChild("LogScroll", ImVec2(0, -25), true);
        ImGui::Text("[INFO] Editor started");
        ImGui::Text("[INFO] Renderer initialized");
        ImGui::EndChild();

        static char inputBuf[256] = "";
        if (ImGui::InputText("Command", inputBuf, sizeof(inputBuf),
            ImGuiInputTextFlags_EnterReturnsTrue))
        {
            LOG_INFO("Console command: {}", inputBuf);
            inputBuf[0] = '\0';
        }

        ImGui::End();
    }
} // namespace Nightbloom