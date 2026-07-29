//------------------------------------------------------------------------------
// Raycast.hpp
//
// Ray/shape intersection primitives — the spatial-query half of the engine.
// Deliberately narrow: this exists to answer "can A see B", which is the core
// question of the stealth game (guard vision cones, ROADMAP 3.3).
//
// This is NOT a physics engine and is not trying to become one. There is no
// broadphase, no swept shapes, no contact resolution. Jolt is still not
// vendored; when a prototype needs rigid bodies, that's the moment to add it.
// Line-of-sight needs none of that — it needs a ray, some boxes, and the
// terrain heightfield we already read back to the CPU.
//
// Everything here is header-only pure math with no engine dependencies, so it
// is trivially unit-testable. Scene-level queries live in Scene::Raycast.
//------------------------------------------------------------------------------
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <functional>
#include <algorithm>   // std::min
#include <utility>     // std::swap
#include <cmath>

namespace Nightbloom
{
	//--------------------------------------------------------------------------
	// A ray. `direction` is expected normalized for world-space queries, so `t`
	// comes back in world units (metres) and can be compared against distances.
	//--------------------------------------------------------------------------
	struct Ray
	{
		glm::vec3 origin{ 0.0f };
		glm::vec3 direction{ 0.0f, 0.0f, -1.0f };

		Ray() = default;
		Ray(const glm::vec3& o, const glm::vec3& d) : origin(o), direction(d) {}

		// Build a ray between two points; `outLength` receives their distance so
		// callers can bound the query at the target instead of marching past it.
		static Ray Between(const glm::vec3& from, const glm::vec3& to, float& outLength)
		{
			glm::vec3 delta = to - from;
			outLength = glm::length(delta);
			// Degenerate (from == to): any direction works, the query is a no-op.
			glm::vec3 dir = (outLength > 1e-6f) ? (delta / outLength) : glm::vec3(0, 0, -1);
			return Ray(from, dir);
		}

		glm::vec3 At(float t) const { return origin + direction * t; }
	};

	//--------------------------------------------------------------------------
	// Result of a query. `objectIndex` is the Scene object index that was hit,
	// or -1 for terrain / no hit — see Scene::Raycast.
	//--------------------------------------------------------------------------
	struct RayHit
	{
		bool      hit = false;
		float     t = 0.0f;               // distance along the ray
		glm::vec3 point{ 0.0f };            // world-space hit position
		glm::vec3 normal{ 0.0f, 1.0f, 0.0f }; // world-space surface normal
		int       objectIndex = -1;       // Scene index, or -1 for terrain
		bool      isTerrain = false;
	};

	//--------------------------------------------------------------------------
	// Ray vs axis-aligned box (slab method), in whatever space both are given.
	//
	// Returns the nearest intersection in [0, maxT]. A ray starting INSIDE the
	// box counts as a hit at t=0 — for line-of-sight that's the correct answer
	// (you can't see out of something you're inside), and it avoids the classic
	// bug where a guard standing in a crate sees through it.
	//--------------------------------------------------------------------------
	inline bool RayAABB(const Ray& ray, const glm::vec3& boxMin, const glm::vec3& boxMax,
		float maxT, float& outT, glm::vec3& outNormal)
	{
		float tMin = 0.0f;
		float tMax = maxT;
		int   hitAxis = 0;
		float hitSign = -1.0f;

		for (int axis = 0; axis < 3; ++axis)
		{
			// Ray parallel to this slab: no hit unless the origin is already
			// between the planes. 1/0 would give ±inf and (inf - inf) = NaN,
			// which silently poisons the comparisons below.
			if (std::abs(ray.direction[axis]) < 1e-8f)
			{
				if (ray.origin[axis] < boxMin[axis] || ray.origin[axis] > boxMax[axis])
					return false;
				continue;
			}

			float invD = 1.0f / ray.direction[axis];
			float t1 = (boxMin[axis] - ray.origin[axis]) * invD;
			float t2 = (boxMax[axis] - ray.origin[axis]) * invD;
			float sign = -1.0f;              // entering through the min plane
			if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }

			if (t1 > tMin) { tMin = t1; hitAxis = axis; hitSign = sign; }
			if (t2 < tMax) { tMax = t2; }
			if (tMin > tMax) return false;
		}

		outT = tMin;
		outNormal = glm::vec3(0.0f);
		outNormal[hitAxis] = hitSign;
		return true;
	}

	//--------------------------------------------------------------------------
	// Ray vs oriented box: an AABB in some object's local space, placed by a
	// model matrix.
	//
	// The ray is pushed into local space rather than the box into world space —
	// one inverse-matrix transform beats rebuilding 8 corners, and it stays
	// exact under rotation (a world-space AABB of a rotated box is fatter than
	// the box, which would occlude sight lines that should be clear).
	//
	// The local direction is deliberately NOT renormalized: leaving it
	// unnormalized makes local `t` numerically equal to world `t`, so the
	// returned distance is still in world units even under non-uniform scale.
	//--------------------------------------------------------------------------
	inline bool RayOBB(const Ray& ray, const glm::vec3& boxMin, const glm::vec3& boxMax,
		const glm::mat4& modelMatrix, float maxT, float& outT, glm::vec3& outNormal)
	{
		glm::mat4 inv = glm::inverse(modelMatrix);

		Ray localRay;
		localRay.origin = glm::vec3(inv * glm::vec4(ray.origin, 1.0f));
		localRay.direction = glm::vec3(inv * glm::vec4(ray.direction, 0.0f));

		float     localT = 0.0f;
		glm::vec3 localNormal(0.0f);
		if (!RayAABB(localRay, boxMin, boxMax, maxT, localT, localNormal))
			return false;

		outT = localT;

		// Normals transform by the inverse-transpose, otherwise non-uniform
		// scale tilts them off the surface.
		glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(modelMatrix));
		glm::vec3 worldNormal = normalMatrix * localNormal;
		float len = glm::length(worldNormal);
		outNormal = (len > 1e-8f) ? (worldNormal / len) : glm::vec3(0.0f, 1.0f, 0.0f);
		return true;
	}

	//--------------------------------------------------------------------------
	// Ray vs terrain heightfield, by marching.
	//
	// `heightFn(x, z) -> world Y` is injected rather than taking a TerrainSystem,
	// matching the GroundSampler convention the pawns already use — so this works
	// on a flat floor, on terrain, or in a scene with no terrain at all.
	//
	// Method: step along the ray tracking whether we're above or below the
	// surface; a sign flip brackets a crossing, then bisect to refine. This is
	// approximate by nature — `step` trades cost against the chance of stepping
	// clean over a thin ridge. The default (0.5 m) is tuned for human-scale
	// sight lines over terrain whose features are metres wide, not centimetres.
	//
	// A ray that starts underground returns a hit at t=0: you cannot see out
	// from inside a hill.
	//--------------------------------------------------------------------------
	inline bool RayTerrain(const Ray& ray, const std::function<float(float, float)>& heightFn,
		float maxT, float& outT, float step = 0.5f, int refineIterations = 12)
	{
		if (!heightFn || maxT <= 0.0f) return false;
		if (step <= 1e-4f) step = 0.5f;

		auto SignedHeight = [&](float t)
		{
			glm::vec3 p = ray.At(t);
			return p.y - heightFn(p.x, p.z);   // >0 above ground, <0 below
		};

		float prevT = 0.0f;
		float prevSigned = SignedHeight(0.0f);

		// Already below the surface at the origin.
		if (prevSigned < 0.0f) { outT = 0.0f; return true; }

		while (prevT < maxT)
		{
			float currT = std::min(prevT + step, maxT);
			float currSigned = SignedHeight(currT);

			if (currSigned < 0.0f)
			{
				// Crossing bracketed in (prevT, currT] — bisect to refine.
				float lo = prevT, hi = currT;
				for (int i = 0; i < refineIterations; ++i)
				{
					float mid = 0.5f * (lo + hi);
					if (SignedHeight(mid) < 0.0f) hi = mid; else lo = mid;
				}
				outT = hi;
				return true;
			}

			if (currT >= maxT) break;
			prevT = currT;
			prevSigned = currSigned;
		}

		return false;
	}
}
