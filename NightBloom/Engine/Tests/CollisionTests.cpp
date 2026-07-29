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
