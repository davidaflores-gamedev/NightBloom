// Panels/ShaderCompilerPanel.cpp
#include "ShaderCompilerPanel.hpp"

#include "../Tools/ShaderEditor/ShaderNodeEditor.hpp"
#include "../EditorFileUtils.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanShader.hpp"
#include "Engine/Renderer/Vulkan/VulkanPipeline.hpp"
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include "Engine/Core/Logger/Logger.hpp"
#include <imgui.h>

namespace Nightbloom
{
    ShaderCompilerPanel::ShaderCompilerPanel() = default;
    ShaderCompilerPanel::~ShaderCompilerPanel() = default;

    void ShaderCompilerPanel::Initialize()
    {
        m_NodeEditor = std::make_unique<ShaderNodeEditor>();
    }

    void ShaderCompilerPanel::Draw(EditorContext& ctx)
    {
        // Draw the node editor graph window when open
        if (isOpen && m_NodeEditor)
        {
            m_NodeEditor->Draw("Shader Node Editor", &isOpen);
            DrawCompilerWindow(ctx);
        }
    }

    void ShaderCompilerPanel::DrawCompilerWindow(EditorContext& ctx)
    {
        ImGui::Begin("Shader Compiler");

        ImGui::Text("Node Graph Shader Compiler");
        if (ctx.projectName)
            ImGui::Text("Target Project: %s", ctx.projectName->c_str());
        ImGui::Separator();

        if (ImGui::Button("Compile & Apply", ImVec2(150, 30)))
            CompileAndApply(ctx);

        ImGui::SameLine();
        if (ImGui::Button("Reload Shaders", ImVec2(150, 30)))
            if (ctx.renderer) ctx.renderer->ReloadShaders();

        // Status
        if (m_CompileSuccess)
        {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Compilation successful!");
            if (ctx.projectName)
                ImGui::Text("Deployed to: %s/Shaders/", ctx.projectName->c_str());
        }
        else if (m_CompileError)
        {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Compilation failed!");
            if (!m_LastError.empty())
                ImGui::TextWrapped("Error: %s", m_LastError.c_str());
        }

        ImGui::Separator();
        ImGui::Text("Instructions:");
        ImGui::BulletText("Right-click canvas to add nodes");
        ImGui::BulletText("Drag from output to input pins");
        ImGui::BulletText("Middle mouse to pan");
        ImGui::BulletText("Select node + Delete to remove");

        ImGui::End();
    }

    bool ShaderCompilerPanel::CompileAndApply(EditorContext& ctx)
    {
        if (!m_NodeEditor)
        {
            m_CompileError = true;
            m_LastError = "Shader node editor not initialized";
            return false;
        }

        if (!m_NodeEditor->CompileShaders())
        {
            m_CompileSuccess = false;
            m_CompileError = true;
            m_LastError = m_NodeEditor->GetLastError();
            return false;
        }

        bool vertOk = Editor::EditorFileUtils::SaveShaderFile(
            "NodeGenerated.vert", m_NodeEditor->GetVertexShader());
        bool fragOk = Editor::EditorFileUtils::SaveShaderFile(
            "NodeGenerated.frag", m_NodeEditor->GetFragmentShader());

        if (!vertOk || !fragOk)
        {
            m_CompileSuccess = false;
            m_CompileError = true;
            m_LastError = "Failed to compile shaders to SPIR-V";
            return false;
        }

        if (!ctx.renderer)
        {
            m_CompileError = true;
            m_LastError = "Renderer not available";
            return false;
        }

        ResourceManager* resources = ctx.renderer->GetResourceManager();
        if (!resources)
        {
            m_CompileError = true;
            m_LastError = "Resource manager not available";
            return false;
        }

        resources->DestroyShader("node_vert");
        resources->DestroyShader("node_frag");

        VulkanShader* vertShader = resources->LoadShader(
            "node_vert", ShaderStage::Vertex, "NodeGenerated.vert.spv");
        VulkanShader* fragShader = resources->LoadShader(
            "node_frag", ShaderStage::Fragment, "NodeGenerated.frag.spv");

        if (!vertShader || !fragShader)
        {
            m_CompileError = true;
            m_LastError = "Failed to load compiled shaders into resource manager";
            return false;
        }

        PipelineConfig config;
        config.vertexShader = vertShader;
        config.fragmentShader = fragShader;
        config.useVertexInput = true;
        config.topology = PrimitiveTopology::TriangleList;
        config.polygonMode = PolygonMode::Fill;
        config.cullMode = CullMode::Back;
        config.frontFace = FrontFace::CounterClockwise;
        config.depthTestEnable = true;
        config.depthWriteEnable = true;
        config.depthCompareOp = CompareOp::GreaterOrEqual;
        config.useTextures = true; // always bind one, command recorder binds 1 unconditionally for node generated
        config.useUniformBuffer = true;
        config.pushConstantSize = sizeof(PushConstantData);
        config.pushConstantStages = ShaderStage::VertexFragment;

        if (ctx.renderer->GetPipelineManager()->CreatePipeline(
            PipelineType::NodeGenerated, config))
        {
            LOG_INFO("NodeGenerated pipeline created and applied");
            m_CompileSuccess = true;
            m_CompileError = false;
            m_LastError.clear();
            return true;
        }

        m_CompileError = true;
        m_LastError = "Failed to create NodeGenerated pipeline";
        return false;
    }
} // namespace Nightbloom