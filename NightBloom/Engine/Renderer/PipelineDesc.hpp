// Renderer/PipelineDesc.hpp  (include from editor/game)
//#pragma once
//#include <string>
//#include <vector>
//#include <cstdint>
//
//namespace Nightbloom
//{
//	// Keep backend-agnostic enums
//	enum class CullMode { None, Back, Front, FrontAndBack };
//	enum class CompareOp { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };
//	enum ShaderStage : uint32_t { VS = 1u << 0, FS = 1u << 1 };  // bitmask
//
//	struct RasterState {
//		bool     depthTest = true;
//		bool     depthWrite = true;
//		CullMode cull = CullMode::Back;
//		CompareOp depthFunc = CompareOp::LessOrEqual;
//	};
//
//	struct TextureBinding { uint32_t set = 0, binding = 0; uint32_t stages = FS; };
//
//	struct PushConstRange { uint32_t offset = 0, size = 0; uint32_t stages = VS | FS; };
//
//	struct PipelineDesc {
//		std::string vertSpirvPath;      // absolute or runtime-relative
//		std::string fragSpirvPath;
//		bool        useVertexInput = true;
//		RasterState raster;
//		std::vector<TextureBinding> textures;     // what the shaders declare
//		std::vector<PushConstRange> pushConsts;   // match your GLSL
//		std::string renderPassName;               // optional: which pass
//	};
//}