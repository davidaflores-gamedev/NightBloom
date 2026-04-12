// Panels/ShaderCompilerPanel.hpp
#pragma once
#include "../EditorContext.hpp"
#include "Engine/Renderer/PipelineInterface.hpp"
#include <memory>
#include <string>

namespace Nightbloom
{
    class ShaderNodeEditor;

    class ShaderCompilerPanel
    {
    public:
        ShaderCompilerPanel();
        ~ShaderCompilerPanel();

        bool isOpen = false;  // Node editor window visibility
        bool isEditorOpen = false;  // Compiler panel window visibility

        void Draw(EditorContext& ctx);

        // Called from EditorApp::OnStartup()
        void Initialize();

    private:
        std::unique_ptr<ShaderNodeEditor> m_NodeEditor;

        bool        m_CompileSuccess = false;
        bool        m_CompileError = false;
        std::string m_LastError;

        // Builds and hot-loads the NodeGenerated pipeline.
        // Returns true on success.
        bool CompileAndApply(EditorContext& ctx);

        void DrawNodeEditorWindow();
        void DrawCompilerWindow(EditorContext& ctx);
    };
} // namespace Nightbloom