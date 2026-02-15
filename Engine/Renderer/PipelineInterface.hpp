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