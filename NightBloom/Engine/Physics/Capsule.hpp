//------------------------------------------------------------------------------
// Capsule.hpp
//
// Capsule / segment / sphere overlap and push-out. Companion to Collision.hpp
// (character-vs-world boxes) and Raycast.hpp (line of sight).
//
// WHY CAPSULES: they're the standard shape for fighting-game hurtboxes and
// hitboxes, and for character-vs-character pushboxes. A limb is a capsule, a
// torso is a capsule, a fist is a sphere (a capsule with a = b). The overlap
// test is one segment-segment closest-point query plus a radius compare, it's
// rotation-invariant, and it has no edge cases at corners the way boxes do.
//
// A sphere is a capsule whose two end points coincide, so every function here
// accepts degenerate capsules and the sphere overloads simply forward.
//
// Header-only pure math, no engine dependencies, so it unit-tests directly.
//------------------------------------------------------------------------------
#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace Nightbloom
{
	//--------------------------------------------------------------------------
	// A capsule: every point within `radius` of the segment a-b.
	//--------------------------------------------------------------------------
	struct Capsule
	{
		glm::vec3 a{ 0.0f };
		glm::vec3 b{ 0.0f };
		float     radius = 0.0f;

		static Capsule Sphere(const glm::vec3& center, float r) { return { center, center, r }; }

		// Vertical capsule standing on `feet`, total height `height` (end caps
		// included). Height below 2*radius collapses to a sphere.
		static Capsule Upright(const glm::vec3& feet, float r, float height)
		{
			const float h = std::max(height - 2.0f * r, 0.0f);
			const glm::vec3 lo = feet + glm::vec3(0.0f, r, 0.0f);
			return { lo, lo + glm::vec3(0.0f, h, 0.0f), r };
		}
	};

	//--------------------------------------------------------------------------
	// Closest point on segment a-b to point p.
	//--------------------------------------------------------------------------
	inline glm::vec3 ClosestPointOnSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b)
	{
		const glm::vec3 ab = b - a;
		const float len2 = glm::dot(ab, ab);
		if (len2 < 1e-12f) return a;
		const float t = std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f);
		return a + ab * t;
	}

	//--------------------------------------------------------------------------
	// Closest points between segments p1-q1 and p2-q2. Writes them to c1/c2 and
	// returns the squared distance between them.
	//
	// Ericson, "Real-Time Collision Detection" §5.1.9. Handles either or both
	// segments being degenerate (points) and parallel segments.
	//--------------------------------------------------------------------------
	inline float ClosestPointsSegmentSegment(const glm::vec3& p1, const glm::vec3& q1,
		const glm::vec3& p2, const glm::vec3& q2, glm::vec3& c1, glm::vec3& c2)
	{
		constexpr float kEps = 1e-12f;
		const glm::vec3 d1 = q1 - p1;
		const glm::vec3 d2 = q2 - p2;
		const glm::vec3 r = p1 - p2;
		const float a = glm::dot(d1, d1);
		const float e = glm::dot(d2, d2);
		const float f = glm::dot(d2, r);

		float s = 0.0f, t = 0.0f;

		if (a <= kEps && e <= kEps)
		{
			// Both points.
			c1 = p1; c2 = p2;
			return glm::dot(c1 - c2, c1 - c2);
		}
		if (a <= kEps)
		{
			// First is a point.
			t = std::clamp(f / e, 0.0f, 1.0f);
		}
		else
		{
			const float c = glm::dot(d1, r);
			if (e <= kEps)
			{
				// Second is a point.
				s = std::clamp(-c / a, 0.0f, 1.0f);
			}
			else
			{
				const float b = glm::dot(d1, d2);
				const float denom = a * e - b * b;   // >= 0; 0 when parallel
				s = (denom > kEps) ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
				t = (b * s + f) / e;
				if (t < 0.0f)      { t = 0.0f; s = std::clamp(-c / a, 0.0f, 1.0f); }
				else if (t > 1.0f) { t = 1.0f; s = std::clamp((b - c) / a, 0.0f, 1.0f); }
			}
		}

		c1 = p1 + d1 * s;
		c2 = p2 + d2 * t;
		return glm::dot(c1 - c2, c1 - c2);
	}

	//--------------------------------------------------------------------------
	// Overlap test only — what a hitbox-vs-hurtbox check needs. Touching
	// (distance == r1 + r2) counts as no overlap.
	//--------------------------------------------------------------------------
	inline bool OverlapCapsules(const Capsule& x, const Capsule& y)
	{
		glm::vec3 c1, c2;
		const float d2 = ClosestPointsSegmentSegment(x.a, x.b, y.a, y.b, c1, c2);
		const float r = x.radius + y.radius;
		return d2 < r * r;
	}

	//--------------------------------------------------------------------------
	// Push-out — what a pushbox (two bodies that must not overlap) needs.
	//
	// `push` moves capsule X out of capsule Y; split it between the two bodies
	// (e.g. half each) for symmetric separation. `point` is the midpoint of the
	// overlap, useful for spawning a hit spark.
	//
	// If the core segments actually intersect (zero distance), there's no
	// defined direction; we fall back to separating horizontally along the
	// line between the capsules' midpoints, then to +X. For upright character
	// capsules that's the sensible answer: push apart on the ground plane.
	//--------------------------------------------------------------------------
	struct CapsuleContact
	{
		bool      hit = false;
		glm::vec3 push{ 0.0f };                // add to X to separate it from Y
		glm::vec3 normal{ 1.0f, 0.0f, 0.0f };  // from Y towards X
		float     depth = 0.0f;                // |push|
		glm::vec3 point{ 0.0f };               // midpoint of the overlap region
	};

	inline CapsuleContact ResolveCapsules(const Capsule& x, const Capsule& y)
	{
		CapsuleContact c;
		glm::vec3 cx, cy;
		const float d2 = ClosestPointsSegmentSegment(x.a, x.b, y.a, y.b, cx, cy);
		const float r = x.radius + y.radius;
		if (d2 >= r * r) return c;

		const float dist = std::sqrt(d2);
		glm::vec3 n;
		if (dist > 1e-6f)
		{
			n = (cx - cy) / dist;
		}
		else
		{
			glm::vec3 between = (x.a + x.b) * 0.5f - (y.a + y.b) * 0.5f;
			between.y = 0.0f;
			const float len = glm::length(between);
			n = (len > 1e-6f) ? between / len : glm::vec3(1.0f, 0.0f, 0.0f);
		}

		c.hit = true;
		c.normal = n;
		c.depth = r - dist;
		c.push = n * c.depth;
		// Midway between the two surfaces along the contact normal: X's surface
		// point facing Y, and Y's surface point facing X.
		c.point = ((cx - n * x.radius) + (cy + n * y.radius)) * 0.5f;
		return c;
	}

	//--------------------------------------------------------------------------
	// Sphere conveniences (a sphere is a degenerate capsule).
	//--------------------------------------------------------------------------
	inline bool OverlapSphereCapsule(const glm::vec3& center, float radius, const Capsule& cap)
	{
		return OverlapCapsules(Capsule::Sphere(center, radius), cap);
	}

	inline bool OverlapSpheres(const glm::vec3& ca, float ra, const glm::vec3& cb, float rb)
	{
		const glm::vec3 d = ca - cb;
		const float r = ra + rb;
		return glm::dot(d, d) < r * r;
	}

} // namespace Nightbloom
