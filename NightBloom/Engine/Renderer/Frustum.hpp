//------------------------------------------------------------------------------
// Frustum.hpp
//
// View frustum extraction and AABB intersection test, for culling scene
// objects before they're added to the draw list.
//
// Only 5 planes (left, right, top, bottom, near) — this engine uses an
// infinite reverse-Z projection (see Camera::SetPerspectiveInfiniteReverseZ),
// so there is no real far plane to extract.
//------------------------------------------------------------------------------
#pragma once

#include <glm/glm.hpp>
#include <cmath>

namespace Nightbloom
{
	struct Frustum
	{
		// xyz = plane normal (pointing inward), w = distance
		glm::vec4 planes[5];

		static Frustum ExtractFromMatrix(const glm::mat4& viewProj)
		{
			Frustum f;

			// Gribb-Hartmann plane extraction from the rows of viewProj.
			// glm matrices are column-major; row access via [col][row].
			glm::vec4 row0(viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]);
			glm::vec4 row1(viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]);
			glm::vec4 row2(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
			glm::vec4 row3(viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]);

			f.planes[0] = row3 + row0; // left
			f.planes[1] = row3 - row0; // right
			f.planes[2] = row3 - row1; // top
			f.planes[3] = row3 + row1; // bottom
			f.planes[4] = row3 + row2; // near

			for (auto& plane : f.planes)
			{
				float length = glm::length(glm::vec3(plane));
				if (length > 0.0001f)
				{
					plane /= length;
				}
			}

			return f;
		}

		// Conservative AABB-vs-frustum test using center/extents.
		// Returns false only if the box is fully outside at least one plane.
		bool Intersects(const glm::vec3& worldCenter, const glm::vec3& worldExtents) const
		{
			for (const auto& plane : planes)
			{
				float distance = glm::dot(glm::vec3(plane), worldCenter) + plane.w;
				float projectedExtent =
					worldExtents.x * std::abs(plane.x) +
					worldExtents.y * std::abs(plane.y) +
					worldExtents.z * std::abs(plane.z);

				if (distance + projectedExtent < 0.0f)
				{
					return false; // fully outside this plane
				}
			}
			return true;
		}
	};

	// Transforms a local-space AABB (min/max) into a world-space center/extents
	// pair, using the cheap absolute-rotation-matrix method (no need to
	// transform all 8 corners individually).
	inline void TransformAABB(
		const glm::vec3& localMin, const glm::vec3& localMax,
		const glm::mat4& transform,
		glm::vec3& outCenter, glm::vec3& outExtents)
	{
		glm::vec3 localCenter = (localMin + localMax) * 0.5f;
		glm::vec3 localExtents = (localMax - localMin) * 0.5f;

		outCenter = glm::vec3(transform * glm::vec4(localCenter, 1.0f));

		glm::mat3 absRotScale(
			glm::abs(glm::vec3(transform[0])),
			glm::abs(glm::vec3(transform[1])),
			glm::abs(glm::vec3(transform[2]))
		);
		outExtents = absRotScale * localExtents;
	}

} // namespace Nightbloom
