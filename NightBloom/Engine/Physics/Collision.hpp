//------------------------------------------------------------------------------
// Collision.hpp
//
// Character-vs-world collision resolution. Companion to Raycast.hpp: that
// answers "can A see B", this answers "can A stand here".
//
// SCOPE, deliberately narrow: a vertical cylinder (the character) pushed out of
// axis-aligned boxes (the world). No rigid bodies, no dynamic-vs-dynamic, no
// restitution, no swept CCD. That covers walking into walls and around crates,
// which is what a stealth prototype needs. Jolt remains the answer the day
// something needs to tumble, stack, or be thrown.
//
// WHY PUSH-OUT RATHER THAN SWEEP: a swept ray stops the character dead at the
// surface, so walking into a wall at a glancing angle kills all movement.
// Resolving penetration along the minimum axis instead lets the leftover motion
// survive, which is what produces wall sliding for free. Sliding matters more
// than sub-frame accuracy here because speeds are human-scale — at 6 m/s and
// 60 Hz a frame moves 10 cm, far less than the character's radius, so tunnelling
// isn't a realistic failure.
//
// Header-only pure math, no engine dependencies, so it unit-tests directly.
//------------------------------------------------------------------------------
#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Nightbloom
{
	//--------------------------------------------------------------------------
	// A world-space axis-aligned box.
	//--------------------------------------------------------------------------
	struct AABB
	{
		glm::vec3 min{ 0.0f };
		glm::vec3 max{ 0.0f };

		bool IsValid() const { return max.x >= min.x && max.y >= min.y && max.z >= min.z; }
	};

	//--------------------------------------------------------------------------
	// World AABB enclosing a local box placed by `transform`.
	//
	// All 8 corners are transformed rather than just min/max: under rotation the
	// corners of a box do not map to the corners of its bounds, so transforming
	// only two of them silently produces a box that is both wrong and often too
	// small — the failure mode being a character walking through a rotated crate.
	//
	// NOTE this is conservative for rotated geometry (a 45-degree box gets a
	// fatter collider than it deserves). Accepted for now: cover is mostly
	// axis-aligned, and a fat collider fails safe by keeping you out, whereas an
	// exact OBB solve is a lot more machinery. Revisit if rotated cover feels
	// sticky.
	//--------------------------------------------------------------------------
	inline AABB ComputeWorldAABB(const glm::vec3& localMin, const glm::vec3& localMax,
		const glm::mat4& transform)
	{
		AABB out;
		out.min = glm::vec3(std::numeric_limits<float>::max());
		out.max = glm::vec3(std::numeric_limits<float>::lowest());

		for (int i = 0; i < 8; ++i)
		{
			glm::vec3 corner(
				(i & 1) ? localMax.x : localMin.x,
				(i & 2) ? localMax.y : localMin.y,
				(i & 4) ? localMax.z : localMin.z);

			glm::vec3 world = glm::vec3(transform * glm::vec4(corner, 1.0f));
			out.min = glm::min(out.min, world);
			out.max = glm::max(out.max, world);
		}
		return out;
	}

	//--------------------------------------------------------------------------
	// Resolve a vertical cylinder against one box, in XZ only.
	//
	//   center   - cylinder centre in XZ; y component is the FEET height
	//   radius   - cylinder radius
	//   height   - cylinder height above the feet
	//
	// Returns the XZ displacement needed to separate them (zero if already
	// clear). Y is never modified: vertical placement belongs to the ground
	// sampler, and letting collision fight it produces a character that climbs
	// walls or sinks into slopes.
	//
	// The vertical overlap test uses a small bias at the feet so that standing
	// *on top* of a box doesn't register as a side collision and eject you.
	//--------------------------------------------------------------------------
	inline glm::vec3 ResolveCylinderVsAABB(const glm::vec3& center, float radius, float height,
		const AABB& box)
	{
		const glm::vec3 kNoPush(0.0f);
		if (!box.IsValid() || radius <= 0.0f) return kNoPush;

		// Vertical overlap. Feet bias: treat the bottom of the character as
		// slightly above its actual feet so resting on a surface is not a hit.
		const float kFootBias = 0.05f;
		const float bodyMinY = center.y + kFootBias;
		const float bodyMaxY = center.y + height;
		if (bodyMaxY <= box.min.y || bodyMinY >= box.max.y) return kNoPush;

		// Closest point on the box footprint to the cylinder axis.
		const float closestX = std::clamp(center.x, box.min.x, box.max.x);
		const float closestZ = std::clamp(center.z, box.min.z, box.max.z);

		const float dx = center.x - closestX;
		const float dz = center.z - closestZ;
		const float distSq = dx * dx + dz * dz;

		if (distSq > radius * radius) return kNoPush;   // clear

		if (distSq > 1e-8f)
		{
			// Outside the footprint: push straight out from the closest point.
			const float dist = std::sqrt(distSq);
			const float push = radius - dist;
			return glm::vec3((dx / dist) * push, 0.0f, (dz / dist) * push);
		}

		// Centre is INSIDE the footprint (deep penetration, or spawned inside).
		// Eject along whichever face is nearest, so we take the shortest way out
		// instead of picking an arbitrary axis and shoving the character through
		// the box.
		const float toMinX = center.x - box.min.x;
		const float toMaxX = box.max.x - center.x;
		const float toMinZ = center.z - box.min.z;
		const float toMaxZ = box.max.z - center.z;

		float best = toMinX; glm::vec3 axis(-1.0f, 0.0f, 0.0f);
		if (toMaxX < best) { best = toMaxX; axis = glm::vec3(1.0f, 0.0f, 0.0f); }
		if (toMinZ < best) { best = toMinZ; axis = glm::vec3(0.0f, 0.0f, -1.0f); }
		if (toMaxZ < best) { best = toMaxZ; axis = glm::vec3(0.0f, 0.0f, 1.0f); }

		return axis * (best + radius);
	}

	//--------------------------------------------------------------------------
	// Is this box low enough to walk onto rather than into?
	//
	// A box whose top is at or below (feet + stepHeight) is a step: the
	// character should rise onto it. Anything taller is a wall and must be
	// resolved horizontally instead. Without this split, every crate is an
	// impassable wall and the world becomes a maze of knee-high barriers.
	//--------------------------------------------------------------------------
	inline bool IsSteppable(const AABB& box, float feetY, float stepHeight)
	{
		return box.max.y <= feetY + stepHeight;
	}

	//--------------------------------------------------------------------------
	// Height of this box's top surface if the character is standing over it,
	// else -infinity.
	//
	// The footprint is expanded by the character's radius so you can stand on
	// the edge of a crate without your centre being over it — otherwise you'd
	// fall the instant your midpoint cleared the lip, which reads as the world
	// dropping out from under you.
	//
	// `maxRise` bounds how far ABOVE the feet a surface may be and still count,
	// so a table two metres up doesn't teleport you onto it.
	//--------------------------------------------------------------------------
	inline float SurfaceHeightUnder(const glm::vec3& feet, float radius,
		const AABB& box, float maxRise)
	{
		const float kNone = -std::numeric_limits<float>::max();
		if (!box.IsValid()) return kNone;

		// Too high to stand on from here.
		if (box.max.y > feet.y + maxRise) return kNone;

		// Footprint test, expanded by the character's radius.
		const float closestX = std::clamp(feet.x, box.min.x, box.max.x);
		const float closestZ = std::clamp(feet.z, box.min.z, box.max.z);
		const float dx = feet.x - closestX;
		const float dz = feet.z - closestZ;
		if (dx * dx + dz * dz > radius * radius) return kNone;

		return box.max.y;
	}

} // namespace Nightbloom
