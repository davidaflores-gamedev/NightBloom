//------------------------------------------------------------------------------
// BladeMesh.hpp
//
// Generates a single tapered, vertically-segmented, forward-CURVED grass blade
// in local space (VertexPNT, byte-compatible with the engine's hardcoded
// vertex-input layout — see CLAUDE.md's VertexPCU/VertexPNT note). Root at
// origin, blade grows along +Y to height 1.0 while arcing forward in +Z by
// bendAmount (rest-pose curve — a straight strip reads as a geometric spike,
// the arc reads as grass). Width tapers from baseHalfWidth at the root down to
// minTipWidthFraction of that (NOT all the way to a literal point — a
// zero-width tip puts sub-pixel triangles at the very tip, which alias/shimmer
// at a distance; clamping a minimum tip width avoids that, and MSAA on the
// scene pass now also softens the remaining thin edges).
//
// Per-row normals are baked here (mostly up, tilting back as the blade curves)
// instead of the old constant (0,0,1) — the vertex shader rotates them
// per-instance and the fragment shader lights with them, so blades read as a
// soft top-lit surface with per-blade variation rather than shading flat like
// the ground (the old code used the terrain normal for grass).
//
// texCoord.y stores the height fraction (0 at root, 1 at tip), read by the
// shader to weight wind sway, drive a root->tip color gradient, and fake AO.
// texCoord.x stores which side of the blade (0=left, 1=right), kept for the
// future albedo/leaf texture pass.
//
// Usage:
//   BladeMeshData data = BladeMesh::Generate(6, 0.04f, 1.5f, 0.15f, 0.3f);
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
		// bendAmount         - forward (+Z) arc of the rest pose: the tip sits
		//                      bendAmount above-and-forward of the root (local
		//                      units, height normalized to 1.0). 0 = straight.
		//----------------------------------------------------------------------
		static BladeMeshData Generate(uint32_t segments, float baseHalfWidth, float taperPower,
			float minTipWidthFraction = 0.15f, float bendAmount = 0.3f)
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

				// Rest-pose forward arc: quadratic in height so the blade is
				// near-vertical at the root and leans over toward the tip.
				float z = bendAmount * t * t;

				// Baked normal: mostly up (so the field reads as a soft top-lit
				// surface) tilting back (-Z) as the blade curves forward, giving
				// per-blade shading variation once rotated in Grass.vert. Not the
				// exact geometric normal — chosen for look, not physical accuracy.
				glm::vec3 normal = glm::normalize(glm::vec3(0.0f, 1.0f, -1.5f * bendAmount * t));

				VertexPNT left{};
				left.position = glm::vec3(-halfWidth, t, z);
				left.normal = normal;
				left.texCoord = glm::vec2(0.0f, t);
				data.vertices.push_back(left);

				VertexPNT right{};
				right.position = glm::vec3(halfWidth, t, z);
				right.normal = normal;
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
