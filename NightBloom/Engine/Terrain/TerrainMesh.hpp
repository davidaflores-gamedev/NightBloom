//------------------------------------------------------------------------------
// TerrainMesh.hpp
//
// Generates a flat NxN grid of VertexPNT vertices (Y = 0).
// The vertex shader (Terrain.vert) displaces Y at runtime by sampling
// the heightmap texture — so this mesh never needs to be rebuilt.
//
// The grid spans [-halfSize, +halfSize] in X and Z.
// UV [0,1] maps linearly across the grid — the vertex shader uses these
// to sample the heightmap.
//
// Usage:
//   TerrainMeshData data = TerrainMesh::Generate(128, 200.0f);
//   // Upload data.vertices and data.indices to GPU buffers
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/Vertex.hpp"
#include <vector>
#include <cstdint>

namespace Nightbloom
{
	struct TerrainMeshData
	{
		std::vector<VertexPNT> vertices;
		std::vector<uint32_t> indices;

		//Convenience
		uint32_t vertexCount() const { return static_cast<uint32_t>(vertices.size()); }
		uint32_t indexCount() const { return static_cast<uint32_t>(indices.size()); }
	};

	class TerrainMesh
	{
	public:
		//----------------------------------------------------------------------
		// Generate
		//
		// resolution  - number of vertices per side (e.g. 128 → 128x128 grid)
		//               Must be >= 2.
		// worldSize   - total world-space width/depth of the patch (e.g. 200.0)
		//
		// Returns a flat grid ready for GPU upload. The vertex shader will
		// displace Y using the heightmap texture at render time.
		//----------------------------------------------------------------------
		static TerrainMeshData Generate(uint32_t resolution, float worldSize)
		{
			if (resolution < 2) resolution = 2;

			TerrainMeshData data;
			data.vertices.reserve(resolution * resolution);
			data.indices.reserve((resolution - 1) * (resolution - 1) * 6);

			const float halfSize = worldSize * 0.5f;
			const float step = worldSize / static_cast<float>(resolution - 1);
			const float uvStep = 1.0f / static_cast<float>(resolution - 1);

			// ----------------------------------------------------------------
			// Vertices — flat grid, Y = 0
			// Normal is up; the vertex shader overwrites this via finite diff
			// ----------------------------------------------------------------

			for (uint32_t row = 0; row < resolution; ++row)
			{
				for (uint32_t col = 0; col < resolution; ++col)
				{
					VertexPNT v{};
					v.position	= glm::vec3(
						-halfSize + col * step,
						0.0f,
						-halfSize + row * step
					);
					v.normal = glm::vec3(0.0f, 1.0f, 0.0f); // placeholder
					v.texCoord = glm::vec2(col * uvStep, row * uvStep);

					data.vertices.push_back(v);
				}
			}

			// ----------------------------------------------------------------
			// Indices — two triangles per quad, CCW winding
			//
			//  row+1:  tl --- tr
			//           |  \ |
			//  row:    bl --- br
			//
			// ----------------------------------------------------------------
			for (uint32_t row = 0; row < resolution - 1; ++row)
			{
				for (uint32_t col = 0; col < resolution - 1; ++col)
				{
					uint32_t bl = row * resolution + col;
					uint32_t br = row * resolution + col + 1;
					uint32_t tl = (row + 1) * resolution + col;
					uint32_t tr = (row + 1) * resolution + col + 1;

					if ((row + col) % 2 == 0)
					{
						// Diagonal: bl → tr
						data.indices.push_back(bl);
						data.indices.push_back(tl);
						data.indices.push_back(br);

						data.indices.push_back(br);
						data.indices.push_back(tl);
						data.indices.push_back(tr);
					}
					else
					{
						// Diagonal: br → tl  (opposite direction)
						data.indices.push_back(bl);
						data.indices.push_back(tl);
						data.indices.push_back(tr);

						data.indices.push_back(bl);
						data.indices.push_back(tr);
						data.indices.push_back(br);
					}
				}
			}

			return data;
		}
	};
} // namespace Nightbloom