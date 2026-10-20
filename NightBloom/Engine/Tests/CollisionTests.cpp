//------------------------------------------------------------------------------
// CollisionTests.cpp
//
// Character-vs-world collision (Engine/Physics/Collision.hpp).
//------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "Engine/Physics/Collision.hpp"
#include <glm/gtc/matrix_transform.hpp>

using namespace Nightbloom;

namespace
{
	AABB UnitBoxAt(const glm::vec3& center, const glm::vec3& scale)
	{
		return ComputeWorldAABB(glm::vec3(-0.5f), glm::vec3(0.5f),
			glm::translate(glm::mat4(1.0f), center) *
			glm::scale(glm::mat4(1.0f), scale));
	}
}

//------------------------------------------------------------------------------
// ComputeWorldAABB
//------------------------------------------------------------------------------

TEST(WorldAABB, TranslationAndScale)
{
	AABB box = UnitBoxAt(glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(4.0f, 2.0f, 6.0f));

	EXPECT_NEAR(box.min.x, 8.0f, 1e-4f);
	EXPECT_NEAR(box.max.x, 12.0f, 1e-4f);
	EXPECT_NEAR(box.min.y, -1.0f, 1e-4f);
	EXPECT_NEAR(box.max.z, 3.0f, 1e-4f);
}

// Transforming only min/max is the classic bug: under rotation the corners of a
// box do not map to the corners of its bounds, so a 45-degree box would come
// back too small and the character would walk through it.
TEST(WorldAABB, RotationExpandsBounds)
{
	glm::mat4 rot = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f)) *
		glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0, 1, 0));
	AABB box = ComputeWorldAABB(glm::vec3(-0.5f), glm::vec3(0.5f), rot);

	EXPECT_GT(box.max.x, 0.69f) << "a 45-degree unit box reaches ~0.707 on X";
	EXPECT_NEAR(box.max.y, 0.5f, 1e-4f) << "Y is unaffected by a Y-axis rotation";
}

//------------------------------------------------------------------------------
// ResolveCylinderVsAABB
//------------------------------------------------------------------------------

TEST(CylinderCollision, NoPushWhenClear)
{
	AABB box = UnitBoxAt(glm::vec3(0, 1, 0), glm::vec3(2.0f));
	glm::vec3 push = ResolveCylinderVsAABB(glm::vec3(10, 0, 0), 0.35f, 1.8f, box);

	EXPECT_NEAR(glm::length(push), 0.0f, 1e-6f);
}

TEST(CylinderCollision, PushesOutOfWall)
{
	// Box spanning x in [-1, 1]; character overlapping its +X face.
	AABB box = UnitBoxAt(glm::vec3(0, 1, 0), glm::vec3(2.0f, 2.0f, 2.0f));
	glm::vec3 push = ResolveCylinderVsAABB(glm::vec3(1.2f, 0, 0), 0.35f, 1.8f, box);

	EXPECT_GT(push.x, 0.0f) << "should be pushed out along +X";
	EXPECT_NEAR(push.y, 0.0f, 1e-6f) << "collision must never move Y";
	// From x=1.2 with radius 0.35 against a face at x=1.0: needs 0.15 of push.
	EXPECT_NEAR(push.x, 0.15f, 1e-3f);
}

// Vertical separation must be respected, or you'd be shoved sideways by a box
// you are standing on top of, or one buried beneath you.
TEST(CylinderCollision, IgnoresBoxAboveAndBelow)
{
	AABB below = UnitBoxAt(glm::vec3(0, -5.0f, 0), glm::vec3(2.0f));
	AABB above = UnitBoxAt(glm::vec3(0, 10.0f, 0), glm::vec3(2.0f));

	EXPECT_NEAR(glm::length(ResolveCylinderVsAABB(glm::vec3(0, 0, 0), 0.35f, 1.8f, below)), 0.0f, 1e-6f);
	EXPECT_NEAR(glm::length(ResolveCylinderVsAABB(glm::vec3(0, 0, 0), 0.35f, 1.8f, above)), 0.0f, 1e-6f);
}

// Standing on a box must not read as a side hit — that would eject the player
// off every surface they climbed onto.
TEST(CylinderCollision, StandingOnTopIsNotASideCollision)
{
	// Box top at y = 1. Character's feet resting exactly there.
	AABB box = UnitBoxAt(glm::vec3(0, 0.5f, 0), glm::vec3(4.0f, 1.0f, 4.0f));
	glm::vec3 push = ResolveCylinderVsAABB(glm::vec3(0, 1.0f, 0), 0.35f, 1.8f, box);

	EXPECT_NEAR(glm::length(push), 0.0f, 1e-6f);
}

// Deep penetration takes the shortest way out rather than an arbitrary axis.
TEST(CylinderCollision, EjectsFromInsideAlongNearestFace)
{
	// Long in X, thin in Z: the nearest way out of the centre is along Z.
	AABB box = UnitBoxAt(glm::vec3(0, 1, 0), glm::vec3(20.0f, 2.0f, 2.0f));
	glm::vec3 push = ResolveCylinderVsAABB(glm::vec3(0, 0, 0), 0.35f, 1.8f, box);

	EXPECT_GT(std::fabs(push.z), std::fabs(push.x)) << "should exit through the near Z face";
}

// Sliding: approaching a wall at an angle must keep the tangential motion.
// The resolver only cancels the component into the surface.
TEST(CylinderCollision, PushIsPurelyAlongTheSurfaceNormal)
{
	AABB wall = UnitBoxAt(glm::vec3(0, 1, 0), glm::vec3(2.0f, 2.0f, 20.0f));  // faces +/-X
	glm::vec3 push = ResolveCylinderVsAABB(glm::vec3(1.15f, 0, 5.0f), 0.35f, 1.8f, wall);

	EXPECT_GT(push.x, 0.0f);
	EXPECT_NEAR(push.z, 0.0f, 1e-4f) << "no push along the wall, so motion along it survives";
}

//------------------------------------------------------------------------------
// ResolveSphereVsOBB (free-flying ships)
//------------------------------------------------------------------------------
namespace
{
	glm::mat4 BoxTRS(const glm::vec3& pos, float yawDeg, const glm::vec3& scale)
	{
		return glm::translate(glm::mat4(1.0f), pos)
			* glm::rotate(glm::mat4(1.0f), glm::radians(yawDeg), glm::vec3(0, 1, 0))
			* glm::scale(glm::mat4(1.0f), scale);
	}
	const glm::vec3 kUnitMin(-0.5f), kUnitMax(0.5f);
}

TEST(SphereVsOBB, NoContactWhenClear)
{
	SphereContact c = ResolveSphereVsOBB(glm::vec3(3, 0, 0), 1.0f, kUnitMin, kUnitMax, BoxTRS(glm::vec3(0), 0, glm::vec3(2)));
	EXPECT_FALSE(c.hit);
}

TEST(SphereVsOBB, PushesOutAlongFaceNormal)
{
	// Box spans x in [-1, 1]; a radius-1 sphere at x = 1.5 overlaps by 0.5.
	SphereContact c = ResolveSphereVsOBB(glm::vec3(1.5f, 0, 0), 1.0f, kUnitMin, kUnitMax, BoxTRS(glm::vec3(0), 0, glm::vec3(2)));
	ASSERT_TRUE(c.hit);
	EXPECT_NEAR(c.push.x, 0.5f, 1e-4f);
	EXPECT_NEAR(c.push.y, 0.0f, 1e-4f);
	EXPECT_NEAR(c.push.z, 0.0f, 1e-4f);
	EXPECT_NEAR(c.normal.x, 1.0f, 1e-4f);
}

// The point of an OBB test: a 45-degree box must not collide with the empty
// corner region its world AABB would cover.
TEST(SphereVsOBB, RotatedBoxUsesExactShape)
{
	// Unit box turned 45 deg: its faces are at distance 0.5 along the diagonals.
	// (0.6, 0, 0.6) is 0.85 out along a face normal — clear for a 0.3 sphere,
	// yet inside the rotated box's world AABB (which reaches 0.707 on x and z).
	SphereContact c = ResolveSphereVsOBB(glm::vec3(0.6f, 0, 0.6f), 0.3f, kUnitMin, kUnitMax, BoxTRS(glm::vec3(0), 45.0f, glm::vec3(1)));
	EXPECT_FALSE(c.hit);

	SphereContact near = ResolveSphereVsOBB(glm::vec3(0.45f, 0, 0.45f), 0.3f, kUnitMin, kUnitMax, BoxTRS(glm::vec3(0), 45.0f, glm::vec3(1)));
	ASSERT_TRUE(near.hit);
	EXPECT_NEAR(near.normal.x, 0.7071f, 1e-3f);
	EXPECT_NEAR(near.normal.z, 0.7071f, 1e-3f);
}

TEST(SphereVsOBB, RespectsNonUniformScale)
{
	// Box stretched to x in [-2, 2]; sphere at 2.3, radius 0.5 -> push 0.2.
	SphereContact c = ResolveSphereVsOBB(glm::vec3(2.3f, 0, 0), 0.5f, kUnitMin, kUnitMax, BoxTRS(glm::vec3(0), 0, glm::vec3(4, 1, 1)));
	ASSERT_TRUE(c.hit);
	EXPECT_NEAR(c.push.x, 0.2f, 1e-4f);
}

TEST(SphereVsOBB, EjectsFromInsideThroughNearestFace)
{
	// Box half-extents (2, 1, 1); centre at y = 0.8 is 0.2 from the top face.
	SphereContact c = ResolveSphereVsOBB(glm::vec3(0, 0.8f, 0), 0.1f, kUnitMin, kUnitMax, BoxTRS(glm::vec3(0), 0, glm::vec3(4, 2, 2)));
	ASSERT_TRUE(c.hit);
	EXPECT_NEAR(c.normal.y, 1.0f, 1e-4f);
	EXPECT_NEAR(c.push.y, 0.3f, 1e-4f);   // 0.2 to the face + the radius
}

TEST(SphereVsOBB, PushIsPurelyAlongTheSurfaceNormal)
{
	// Grazing a long wall: no push along it, so the motion along it survives.
	SphereContact c = ResolveSphereVsOBB(glm::vec3(1.3f, 0, 5.0f), 0.5f, kUnitMin, kUnitMax, BoxTRS(glm::vec3(0), 0, glm::vec3(2, 2, 20)));
	ASSERT_TRUE(c.hit);
	EXPECT_GT(c.push.x, 0.0f);
	EXPECT_NEAR(c.push.z, 0.0f, 1e-4f);
}
