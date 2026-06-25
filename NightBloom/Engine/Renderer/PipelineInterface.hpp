//------------------------------------------------------------------------------
// PipelineInterface.hpp
//
// Generic pipeline interface that hides backend-specific details
// This is what game/editor code should use
//------------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>
#include <memory>

namespace Nightbloom
{
	enum class PrimitiveTopology
	{
		TriangleList,
		TriangleStrip,
		LineList,
		LineStrip,
		PointList
	};

	enum class PolygonMode
	{
		Fill,
		Line,
		Point
	};

	enum class CullMode
	{
		None,
		Front,
		Back,
		FrontAndBack
	};

	enum class FrontFace
	{
		Clockwise,
		CounterClockwise
	};

	enum class CompareOp
	{
		Never,
		Less,
		Equal,
		LessOrEqual,
		Greater,
		NotEqual,
		GreaterOrEqual,
		Always
	};

	enum class BlendFactor
	{
		Zero,
		One,
		SrcColor,
		OneMinusSrcColor,
		DstColor,
		OneMinusDstColor,
		SrcAlpha,
		OneMinusSrcAlpha,
		DstAlpha,
		OneMinusDstAlpha
	};

	enum class ShaderStage
	{
		Vertex = 0x01,
		Fragment = 0x02,
		Geometry = 0x04,
		Compute = 0x08,
		TessControl = 0x10,
		TessEval = 0x20,

		// Common combinations
		VertexFragment = Vertex | Fragment
	};

	// Use bitwise operations for ShaderStage
	inline ShaderStage operator|(ShaderStage a, ShaderStage b)
	{
		return static_cast<ShaderStage>(static_cast<int>(a) | static_cast<int>(b));
	}

	inline ShaderStage operator&(ShaderStage a, ShaderStage b)
	{
		return static_cast<ShaderStage>(static_cast<int>(a) & static_cast<int>(b));
	}

	// static pipeline types that are used in the engine base pipeline
	enum class PipelineType
	{
		//Old
		Triangle,

		// Current
		Mesh,
		Transparent,


		// Future
		Shadow,
		Skybox,
		Volumetric,
		PostProcess,
		Compute,

		// Editors
		NodeGenerated,
		Terrain,
		TerrainShadow,
		Foliage,        // Instanced grass blades, per-instance data from a storage buffer,
		                // height/slope sampled from the terrain heightmap in the vertex
		                // stage. Drawn after Terrain so it sits on the displaced surface,
		                // before Clouds/Firefly per the existing sort-by-enum-value order.

		Water,          // Horizontal reflective water plane. Drawn after Foliage (sits on/above
		                // the terrain surface, depth-tests against it) but before Clouds (water
		                // reflects the sky) and Firefly. Alpha-blended, depth-tested but does
		                // not write depth. Samples the planar-reflection target rendered from a
		                // mirror-flipped camera. Sets: uniform=0, lighting=1, reflection=2.

		// VFX
		Clouds,         // Full-screen raymarched sky volume, drawn after opaque/terrain so the
		                // normal depth test occludes it correctly; before Firefly so fireflies
		                // composite on top of clouds, not the reverse.
		Firefly,        // Instanced billboard quads, additive blend, agent data from a storage buffer

		Count
	};

	class Shader;

	// Generic pipeline configuration
	struct PipelineConfig
	{
		// Option 1: Shader objects (preferred when available)
		Shader* vertexShader = nullptr;   // NEW
		Shader* fragmentShader = nullptr; // NEW

		// Shader paths
		std::string vertexShaderPath;
		std::string fragmentShaderPath;
		std::string geometryShaderPath;  // Optional
		std::string computeShaderPath;   // For compute pipelines

		// Vertex input
		bool useVertexInput = false;

		// Primitive assembly
		PrimitiveTopology topology = PrimitiveTopology::TriangleList;

		// Rasterization state
		PolygonMode polygonMode = PolygonMode::Fill;
		CullMode cullMode = CullMode::Back;
		FrontFace frontFace = FrontFace::CounterClockwise;
		float lineWidth = 1.0f;

		// Depth testing (configured for reverse-Z by default)
		bool depthTestEnable = true;
		bool depthWriteEnable = true;
		CompareOp depthCompareOp = CompareOp::GreaterOrEqual;  // Reverse-Z: near=1.0, far=0.0

		// Depth bias for shadow mapping(reduces shadow acne)
		bool depthBiasEnable = false;
		float depthBiasConstant = 0.0f;
		float depthBiasSlope = 0.0f;
		float depthBiasClamp = 0.0f;

		// Blending
		bool blendEnable = false;
		BlendFactor srcColorBlendFactor = BlendFactor::SrcAlpha;
		BlendFactor dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;

		// Push constants
		uint32_t pushConstantSize = 0;
		ShaderStage pushConstantStages = ShaderStage::Vertex;

		// Replace descriptorSetCount with more specific flags
		bool useTextures = false;        // Pipeline uses texture sampling
		bool useUniformBuffer = false;   // Pipeline uses uniform buffers
		bool useLighting = false;	// Pipeline uses scene lighting UBO (set 2)
		bool useShadowMap = false;  // Pipeline samples shadow map (set 3)
		bool useHeightmap = false;  // Pipeline samples heightmap in vertex stage (set 4)
		bool useFireflyStorage = false;  // Pipeline reads the firefly agent storage buffer (vertex+compute stages)
		bool useFoliageStorage = false;  // Pipeline reads the foliage instance storage buffer (vertex stage only)
		bool useCloudResult = false;  // Pipeline samples the low-res cloud raymarch result (fragment stage) - the graphics Clouds composite pass's only texture input
		bool usePostProcessInput = false;  // Pipeline samples the scene-color texture (fragment stage) - the PostProcess/FXAA pass's only texture input
		bool useReflectionInput = false;  // Pipeline samples the planar-reflection target (fragment stage) - the Water pass; lands last so it's set 2 (after uniform=0, lighting=1)

		bool hasColorAttachment = true;  // False for depth-only passes (shadow)

		// Optional: custom render pass name (backend will resolve)
		std::string renderPassName;  // Empty = use default
	};

	// Forward declaration - actual implementation is backend-specific
	class IPipeline
	{
	public:
		virtual ~IPipeline() = default;
		virtual bool IsValid() const = 0;
		virtual PipelineType GetType() const = 0;
	};

	// Pipeline manager interface
	class IPipelineManager
	{
	public:
		virtual ~IPipelineManager() = default;

		// Pipeline management
		virtual bool CreatePipeline(PipelineType type, const PipelineConfig& config) = 0;
		virtual bool DestroyPipeline(PipelineType type) = 0;
		virtual IPipeline* GetPipeline(PipelineType type) = 0;

		// Hot reload
		virtual bool ReloadPipeline(PipelineType type) = 0;
		virtual bool ReloadAllPipelines() = 0;

		// Usage (command buffer operations are backend-specific and handled internally)
		virtual void SetActivePipeline(PipelineType type) = 0;

		// Push constants (data is opaque to the interface)
		virtual void PushConstants(PipelineType type, ShaderStage stages,
			const void* data, uint32_t size) = 0;
	};

	// Factory function - implemented by the backend
	std::unique_ptr<IPipelineManager> CreatePipelineManager();
}