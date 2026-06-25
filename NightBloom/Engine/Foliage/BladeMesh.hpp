//------------------------------------------------------------------------------
// BladeMesh.hpp
//
// Generates a single tapered, vertically-segmented grass blade in local
// space (VertexPNT, byte-compatible with the engine's hardcoded vertex-input
// layout — see CLAUDE.md's VertexPCU/VertexPNT note). Root at origin, blade
// grows along +Y to height 1.0; width tapers from baseHalfWidth at the root
// down to minTipWidthFraction of that (NOT all the way to a literal point —
// a zero-width tip puts sub-pixel triangles at the very tip, which alias/
// shimmer at a distance since they flicker in and out of pixel coverage
// frame to frame; clamping a minimum tip width avoids that). Rest pose is
// straight — wind bend and per-instance scale/rotation are applied
// dynamically in Grass.vert, not baked in here.
//
// texCoord.y stores the height fraction (0 at root, 1 at tip), read by the
// shader to weight wind sway and to drive a root->tip color gradient.
// texCoord.x stores which side of the blade (0=left, 1=right), unused for
// now but kept for a future albedo texture pass.
//
// Usage:
//   BladeMeshData data = BladeMesh::Generate(4, 0.05f, 1.5f);
//   // Upload data.vertices and data.indices to GPU buffers, shared by every
//   // grass instance (one mesh, drawn N times via instancing).
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/Vertex.hpp"
#include <vector>
#include <cstdint>
#include <cmath>

namespace Nightbloom
{
	struct BladeMeshData
	{
		std::vector<VertexPNT> vertices;
		std::vector<uint32_t> indices;

		uint32_t vertexCount() const { return static_cast<uint32_t>(vertices.size()); }
		uint32_t indexCount() const { return static_cast<uint32_t>(indices.size()); }
	};

	class BladeMesh
	{
	public:
		//----------------------------------------------------------------------
		// Generate
		//
		// segments           - number of height segments (>=1). More segments
		//                      give a smoother wind bend at the cost of more
		//                      vertices.
		// baseHalfWidth      - half-width at the root, in local units (blade
		//                      height is normalized to 1.0, so this is
		//                      relative to that).
		// taperPower         - exponent on (1-t) for the width falloff curve;
		//                      1.0 is linear, >1.0 keeps the blade wider near
		//                      the root and narrows more sharply near the tip.
		// minTipWidthFraction - tip half-width as a fraction of baseHalfWidth
		//                      (e.g. 0.15 = tip is 15% as wide as the root).
		//                      Clamped >= 0; 0 reproduces the old
		//                      taper-to-a-point behavior if ever wanted.
		//----------------------------------------------------------------------
		static BladeMeshData Generate(uint32_t segments, float baseHalfWidth, float taperPower,
			float minTipWidthFraction = 0.18f)
		{
			if (segments < 1) segments = 1;
			if (minTipWidthFraction < 0.0f) minTipWidthFraction = 0.0f;

			BladeMeshData data;
			uint32_t rowCount = segments + 1;
			data.vertices.reserve(rowCount * 2);
			data.indices.reserve(segments * 6);

			for (uint32_t row = 0; row < rowCount; ++row)
			{
				float t = static_cast<float>(row) / static_cast<float>(segments);
				float taperCurve = std::pow(1.0f - t, taperPower);
				float widthFraction = minTipWidthFraction + (1.0f - minTipWidthFraction) * taperCurve;
				float halfWidth = baseHalfWidth * widthFraction;

				VertexPNT left{};
				left.position = glm::vec3(-halfWidth, t, 0.0f);
				left.normal = glm::vec3(0.0f, 0.0f, 1.0f);
				left.texCoord = glm::vec2(0.0f, t);
				data.vertices.push_back(left);

				VertexPNT right{};
				right.position = glm::vec3(halfWidth, t, 0.0f);
				right.normal = glm::vec3(0.0f, 0.0f, 1.0f);
				right.texCoord = glm::vec2(1.0f, t);
				data.vertices.push_back(right);
			}

			// Quad strip — two triangles per segment, CCW winding
			for (uint32_t seg = 0; seg < segments; ++seg)
			{
				uint32_t bl = seg * 2 + 0;
				uint32_t br = seg * 2 + 1;
				uint32_t tl = (seg + 1) * 2 + 0;
				uint32_t tr = (seg + 1) * 2 + 1;

				data.indices.push_back(bl);
				data.indices.push_back(tl);
				data.indices.push_back(br);

				data.indices.push_back(br);
				data.indices.push_back(tl);
				data.indices.push_back(tr);
			}

			return data;
		}
	};
} // namespace Nightbloom
