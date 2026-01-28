
//------------------------------------------------------------------------------
// DrawCommandSystem.hpp
//
// A flexible system for submitting draw commands from the application layer
// This replaces hardcoded drawing in RecordCommandBuffer
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/PipelineInterface.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <variant>
#include <algorithm>

#include "Engine/Renderer/Model.hpp"
#include "Engine/Renderer/Material.hpp"
#include "Engine/Renderer/Mesh.hpp"

namespace Nightbloom
{
	// Forward declarations
	class Buffer;
	class Texture;

	//class Material;
	//class Model;
	//class Mesh;

	// ============================================================================
	// Draw Command Types
	// ============================================================================

	// Push constant data - can be extended for different pipelines
	struct PushConstantData
	{
		glm::mat4 model = glm::mat4(1.0f);
		glm::vec4 customData = glm::vec4(0.0f);  // For shader-specific data, no longer time
	};

	struct FrameUniformData
	{
		glm::mat4 view;
		glm::mat4 proj;
		glm::vec4 time;  // x = time, yzw = reserved for new data later
		glm::vec4 cameraPos;  // For specular calculations later
	};

	// A single draw command
	struct DrawCommand
	{
		PipelineType pipeline = PipelineType::Triangle;

		// Vertex data
		Buffer* vertexBuffer = nullptr;
		Buffer* indexBuffer = nullptr;
		uint32_t indexCount = 0;
		uint32_t vertexCount = 0;  // For non-indexed draws

		// Instance data
		uint32_t instanceCount = 1;
		uint32_t firstInstance = 0;

		// Push constants (optional)
		bool hasPushConstants = false;
		PushConstantData pushConstants;

		// Textures (simplified for now - expand later)
		std::vector<Texture*> textures;

		// Custom render state overrides (optional)
		std::function<void()> preDrawCallback = nullptr;  // Called before draw
		std::function<void()> postDrawCallback = nullptr; // Called after draw
	};

	// ============================================================================
	// Drawable Interface - Objects that can be rendered
	// ============================================================================

	class IDrawable
	{
	public:
		virtual ~IDrawable() = default;

		// Get draw commands for this object
		virtual std::vector<DrawCommand> GetDrawCommands() const = 0;

		// Update any frame-dependent data
		virtual void Update(float deltaTime) { (void)deltaTime; }

		// Check if should be drawn
		virtual bool IsVisible() const { return true; }
	};

	// ============================================================================
	// Basic Drawable Implementations
	// ============================================================================

	// Simple mesh drawable
	class MeshDrawable : public IDrawable
	{
	public:
		MeshDrawable(Buffer* vb, Buffer* ib, uint32_t indexCount, PipelineType pipeline)
			: m_VertexBuffer(vb), m_IndexBuffer(ib), m_IndexCount(indexCount), m_Pipeline(pipeline)
		{
		}

		std::vector<DrawCommand> GetDrawCommands() const override
		{
			DrawCommand cmd;
			cmd.pipeline = m_Pipeline;
			cmd.vertexBuffer = m_VertexBuffer;
			cmd.indexBuffer = m_IndexBuffer;
			cmd.indexCount = m_IndexCount;
			cmd.hasPushConstants = true;
			cmd.pushConstants = m_PushConstants;

			cmd.textures = m_Textures;

			return { cmd };
		}

		// Add texture management
		void AddTexture(Texture* texture)
		{
			m_Textures.push_back(texture);
		}

		void ClearTextures()
		{
			m_Textures.clear();
		}

		void Update(float deltaTime) override
		{
			m_PushConstants.customData.x += deltaTime; // Example: animate custom data
			// Update logic if needed
		}

		void SetTransform(const glm::mat4& transform) { m_PushConstants.model = transform; }
		//void SetViewMatrix(const glm::mat4& view) { m_PushConstants.view = view; }
		//void SetProjectionMatrix(const glm::mat4& proj) { m_PushConstants.proj = proj; }

	private:
		Buffer* m_VertexBuffer;
		Buffer* m_IndexBuffer;
		uint32_t m_IndexCount;
		PipelineType m_Pipeline;
		PushConstantData m_PushConstants;
		std::vector<Texture*> m_Textures;
	};

	// Model drawable - renders all meshes in a model
	class ModelDrawable : public IDrawable
	{
	public:
	    ModelDrawable(Model* model) : m_Model(model) {}
	
	    std::vector<DrawCommand> GetDrawCommands() const override
	    {
	        std::vector<DrawCommand> commands;
	        
	        if (!m_Model) return commands;
	
	        for (const auto& mesh : m_Model->GetMeshes())
	        {
	            if (!mesh->IsValid()) continue;
	
				if (mesh->GetName() == "Glass") continue;

	            DrawCommand cmd;
	            cmd.pipeline = PipelineType::Mesh;  // Default, can be overridden by material
	            cmd.vertexBuffer = mesh->GetVertexBuffer();
	            cmd.indexBuffer = mesh->GetIndexBuffer();
	            cmd.indexCount = mesh->GetIndexCount();
	            cmd.hasPushConstants = true;
	            cmd.pushConstants.model = m_Model->GetTransform();
	
	            // Get texture from material
	            Material* mat = mesh->GetMaterial();
	            if (mat)
	            {
	                cmd.pipeline = mat->GetPipeline();
	                
	                if (mat->HasAlbedoTexture())
	                {
	                    cmd.textures.push_back(mat->GetAlbedoTexture());
	                }
	            }
	
	            commands.push_back(cmd);
	        }
	
	        return commands;
	    }
	
	    void SetModel(Model* model) { m_Model = model; }
	    Model* GetModel() const { return m_Model; }
	
	private:
	    Model* m_Model = nullptr;
	};

	// Debug shape drawable (for editor gizmos, etc.)
	class DebugDrawable : public IDrawable
	{
	public:
		enum class Shape { Line, Box, Sphere, Grid };

		DebugDrawable(Shape shape, const glm::vec3& color = glm::vec3(1.0f))
			: m_Shape(shape), m_Color(color)
		{
		}

		std::vector<DrawCommand> GetDrawCommands() const override;

	private:
		Shape m_Shape;
		glm::vec3 m_Color;
		glm::mat4 m_Transform = glm::mat4(1.0f);
	};

	// ============================================================================
	// Draw List - Accumulates draw commands for a frame
	// ============================================================================

	class DrawList
	{
	public:
		DrawList() = default;

		// Add a single draw command
		void AddCommand(const DrawCommand& cmd)
		{
			m_Commands.push_back(cmd);
		}

		// Add commands from a drawable
		void AddDrawable(const IDrawable* drawable)
		{
			if (drawable && drawable->IsVisible())
			{
				auto commands = drawable->GetDrawCommands();
				m_Commands.insert(m_Commands.end(), commands.begin(), commands.end());
			}
		}

		// Add a simple mesh with transform
		void DrawMesh(Buffer* vertexBuffer, Buffer* indexBuffer, uint32_t indexCount,
			PipelineType pipeline, const glm::mat4& transform)
		{
			DrawCommand cmd;
			cmd.pipeline = pipeline;
			cmd.vertexBuffer = vertexBuffer;
			cmd.indexBuffer = indexBuffer;
			cmd.indexCount = indexCount;
			cmd.hasPushConstants = true;
			cmd.pushConstants.model = transform;

			// View and projection should be set elsewhere
			m_Commands.push_back(cmd);
		}

		// Clear the list
		void Clear() { m_Commands.clear(); }

		// Get all commands
		const std::vector<DrawCommand>& GetCommands() const { return m_Commands; }

		// Sort commands by pipeline to minimize state changes
		void SortByPipeline()
		{
			std::sort(m_Commands.begin(), m_Commands.end(),
				[](const DrawCommand& a, const DrawCommand& b) {
					return static_cast<int>(a.pipeline) < static_cast<int>(b.pipeline);
				});
		}

	private:
		std::vector<DrawCommand> m_Commands;
	};

	// ============================================================================
	// Scene Interface - Manages what gets drawn
	// ============================================================================

	class IScene
	{
	public:
		virtual ~IScene() = default;

		// Build draw list for the frame
		virtual void BuildDrawList(DrawList& drawList) = 0;

		// Update scene
		virtual void Update(float deltaTime) = 0;
	};

	// Simple test scene
	class TestScene : public IScene
	{
	public:
		TestScene() = default;

		void AddDrawable(std::unique_ptr<IDrawable> drawable)
		{
			m_Drawables.push_back(std::move(drawable));
		}

		void BuildDrawList(DrawList& drawList) override
		{
			for (const auto& drawable : m_Drawables)
			{
				drawList.AddDrawable(drawable.get());
			}
		}

		void Update(float deltaTime) override
		{
			for (auto& drawable : m_Drawables)
			{
				drawable->Update(deltaTime);
			}
		}

	private:
		std::vector<std::unique_ptr<IDrawable>> m_Drawables;
	};


}