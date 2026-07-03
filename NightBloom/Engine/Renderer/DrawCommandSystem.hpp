
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
#include "Engine/Core/Logger/Logger.hpp"

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
		// Computed once per frame on the CPU rather than per-pixel in a
		// shader (e.g. Clouds.frag's ray reconstruction) — a 4x4 inverse is
		// identical for every pixel in the frame, so doing it per-pixel is
		// pure waste. Appended at the end so existing shaders that don't
		// declare these fields are unaffected (they just don't read them).
		glm::mat4 invView;
		glm::mat4 invProj;
	};

	// ============================================================================
	// Descriptor binding — generic, pipeline-agnostic set binding
	// ============================================================================
	// A DrawCommand declares which descriptor sets it needs and at which set index.
	// The CommandRecorder binds them in a single loop, so it no longer switches on
	// PipelineType at all. Frame-global sets (camera / lighting / shadow map) are
	// per-frame double-buffered and owned by the renderer, so a submitting system
	// can't hold their handle — it names them by tag and the recorder resolves the
	// current frame's set. This is what lets a new GPU system be self-contained in
	// its own *System.cpp instead of requiring edits across the recorder.
	enum class BindingSource : uint8_t
	{
		Explicit,        // bind `set` directly (a set the command/system owns)
		FrameUniform,    // recorder resolves the per-frame FrameUniforms set
		FrameLighting,   // recorder resolves the per-frame SceneLighting set
		FrameShadow,     // recorder resolves the per-frame shadow-map sampler set
		ReflectionInput, // recorder binds the planar-reflection colour-target sampler
		CommandTexture,  // recorder binds cmd.textures[0]'s own descriptor set
	};

	struct DescriptorBinding
	{
		uint32_t        setIndex = 0;
		BindingSource   source   = BindingSource::Explicit;
		VkDescriptorSet set      = VK_NULL_HANDLE;  // used when source == Explicit
	};

	// A single draw command
	struct DrawCommand
	{
		PipelineType pipeline = PipelineType::Mesh;

		// Vertex data
		Buffer* vertexBuffer = nullptr;
		Buffer* indexBuffer = nullptr;
		uint32_t indexCount = 0;
		uint32_t vertexCount = 0;  // For non-indexed draws

		// Instance data
		uint32_t instanceCount = 1;
		uint32_t firstInstance = 0;

		// Camera-frustum visibility. Set false by Scene::BuildDrawList for objects culled
		// against the CAMERA frustum. The main color pass skips these; the SHADOW and
		// REFLECTION passes ignore the flag and still draw them, because a caster off-screen
		// (behind/beside the camera) can still cast a shadow into view or appear in the water
		// reflection. Culling shadow casters by the camera frustum is the classic CSM bug.
		bool cameraVisible = true;

		// Push constants (optional)
		bool hasPushConstants = false;
		PushConstantData pushConstants;

		// Textures (simplified for now - expand later)
		std::vector<Texture*> textures;

		// Descriptor sets this draw needs, by set index. Filled at submit time by the
		// owning system, or by AppendCoreBindings for the built-in Mesh/Transparent/
		// Triangle/NodeGenerated pipelines. The recorder binds these generically — it
		// does not switch on PipelineType. See BindingSource for how each is resolved.
		std::vector<DescriptorBinding> descriptorBindings;

		// Custom render state overrides (optional)
		std::function<void()> preDrawCallback = nullptr;  // Called before draw
		std::function<void()> postDrawCallback = nullptr; // Called after draw
	};

	// Look up the explicit descriptor set a command bound at a given set index (or
	// VK_NULL_HANDLE if none). Used by passes that reuse a command's own set at a
	// different slot — e.g. the TerrainShadow pass rebinds the heightmap (main-pass
	// set 4) at set 1 of the depth-only shadow layout.
	inline VkDescriptorSet FindExplicitBinding(const DrawCommand& cmd, uint32_t setIndex)
	{
		for (const auto& b : cmd.descriptorBindings)
			if (b.setIndex == setIndex && b.source == BindingSource::Explicit)
				return b.set;
		return VK_NULL_HANDLE;
	}

	// Fills descriptorBindings for the engine's built-in pipelines. Self-contained GPU
	// systems (Terrain, Grass, Water, Clouds, Fireflies) declare their own bindings in
	// their SubmitDraw instead — this only covers the generic MeshDrawable/ModelDrawable
	// path, which can emit several pipeline types depending on how it's used.
	inline void AppendCoreBindings(DrawCommand& cmd)
	{
		switch (cmd.pipeline)
		{
		case PipelineType::Mesh:
		case PipelineType::Transparent:
			cmd.descriptorBindings = {
				{ 0, BindingSource::FrameUniform },
				{ 1, BindingSource::CommandTexture },
				{ 2, BindingSource::FrameLighting },
				{ 3, BindingSource::FrameShadow },
			};
			break;
		case PipelineType::NodeGenerated:
			cmd.descriptorBindings = {
				{ 0, BindingSource::FrameUniform },
				{ 1, BindingSource::CommandTexture },
			};
			break;
		case PipelineType::Triangle:
			cmd.descriptorBindings = { { 0, BindingSource::FrameUniform } };
			break;
		default:
			break;
		}
	}

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

			AppendCoreBindings(cmd);
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
			(void)deltaTime;
			// (Previously animated customData.x as debug leftover — removed so
			// customData.rgb is stable for emissive tint, e.g. the moon/sun disc.)
		}

		void SetTransform(const glm::mat4& transform) { m_PushConstants.model = transform; }
		void SetCustomData(const glm::vec4& data) { m_PushConstants.customData = data; }

		// Read-back for serialization (primitives store their transform/custom data
		// in the push constants rather than on the SceneObject).
		const glm::mat4& GetTransform() const { return m_PushConstants.model; }
		const glm::vec4& GetCustomData() const { return m_PushConstants.customData; }
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
	    ModelDrawable(Model* model, Texture* defaultTexture) : m_Model(model), m_DefaultTexture(defaultTexture) {}
	
	    std::vector<DrawCommand> GetDrawCommands() const override
	    {
	        std::vector<DrawCommand> opaqueCommands;
			std::vector<DrawCommand> transparentCommands;

	        if (!m_Model) return opaqueCommands;
	
	        for (const auto& mesh : m_Model->GetMeshes())
	        {
	            if (!mesh->IsValid()) continue;

	            DrawCommand cmd;
	            cmd.vertexBuffer = mesh->GetVertexBuffer();
	            cmd.indexBuffer = mesh->GetIndexBuffer();
	            cmd.indexCount = mesh->GetIndexCount();
	            cmd.hasPushConstants = true;
	            cmd.pushConstants.model = m_Model->GetTransform();
	
	            Material* mat = mesh->GetMaterial();

				bool isTransparent = (mesh->GetName() == "Glass");
				// or check if material is alphamode blend?

				Texture* textureToUse = nullptr;
				if (mat && mat->HasAlbedoTexture())
				{
					textureToUse = mat->GetAlbedoTexture();
				}
				else if (m_DefaultTexture)
				{
					textureToUse = m_DefaultTexture;
				}

				if (isTransparent)
				{
					cmd.pipeline = PipelineType::Transparent;

					if (mat)
					{
						glm::vec4 glassColor = mat->GetAlbedoColor();
						glassColor.a = 0.3f;  // Set transparency
						cmd.pushConstants.customData = glassColor;
					}
					else
					{
						cmd.pushConstants.customData = glm::vec4(0.9f, 0.95f, 1.0f, 0.3f);
					}

					if (textureToUse)
						cmd.textures.push_back(textureToUse);

					AppendCoreBindings(cmd);
					transparentCommands.push_back(cmd);
				}
				else
				{
					cmd.pipeline = PipelineType::Mesh;

					cmd.pushConstants.customData = glm::vec4(0.0f);

					if (textureToUse)
						cmd.textures.push_back(textureToUse);

					AppendCoreBindings(cmd);
					opaqueCommands.push_back(cmd);
				}
	        }
	
			opaqueCommands.insert(opaqueCommands.end(),
				transparentCommands.begin(),
				transparentCommands.end());

	        return opaqueCommands;
	    }
	
	    void SetModel(Model* model) { m_Model = model; }
	    Model* GetModel() const { return m_Model; }
	
	private:
	    Model* m_Model = nullptr;
		Texture* m_DefaultTexture = nullptr;
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

		// Add commands from a drawable. cameraVisible=false keeps the commands in the list
		// (so shadow/reflection passes still draw them) but flags them so the main color pass
		// can skip them — used for objects culled against the camera frustum.
		void AddDrawable(const IDrawable* drawable, bool cameraVisible = true)
		{
			if (drawable && drawable->IsVisible())
			{
				auto commands = drawable->GetDrawCommands();
				for (auto& cmd : commands)
					cmd.cameraVisible = cameraVisible;
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

			AppendCoreBindings(cmd);

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
}