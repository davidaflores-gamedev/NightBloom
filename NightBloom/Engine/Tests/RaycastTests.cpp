//------------------------------------------------------------------------------
// RaycastTests.cpp
//
// Unit tests for Engine/Physics/Raycast.hpp — the spatial-query primitives the
// stealth layer's line-of-sight rests on (ROADMAP 3.3 vision cones).
//
// These are pure math with no Vulkan dependency, which is exactly why they're
// worth having: a silently-wrong raycast doesn't crash, it just makes guards
// see through walls, and that is miserable to debug from gameplay symptoms.
//------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "Engine/Physics/Raycast.hpp"
#include <glm/gtc/matrix_transform.hpp>

using namespace Nightbloom;

namespace
{
	const glm::vec3 kUnitMin(-0.5f);
	const glm::vec3 kUnitMax(0.5f);
}

//------------------------------------------------------------------------------
// RayAABB
//------------------------------------------------------------------------------

TEST(RayAABB, HitsBoxAhead)
{
	Ray r(glm::vec3(-5, 0, 0), glm::vec3(1, 0, 0));
	float t; glm::vec3 n;
	ASSERT_TRUE(RayAABB(r, kUnitMin, kUnitMax, 100.0f, t, n));
	EXPECT_NEAR(t, 4.5f, 1e-4f);          // travels 5 to origin, box face at -0.5
	EXPECT_LT(n.x, -0.9f);                // normal points back along -X
}

TEST(RayAABB, MissesBoxBehind)
{
	Ray r(glm::vec3(-5, 0, 0), glm::vec3(-1, 0, 0));
	float t; glm::vec3 n;
	EXPECT_FALSE(RayAABB(r, kUnitMin, kUnitMax, 100.0f, t, n));
}

TEST(RayAABB, MissesBoxPassingAbove)
{
	Ray r(glm::vec3(-5, 2, 0), glm::vec3(1, 0, 0));
	float t; glm::vec3 n;
	EXPECT_FALSE(RayAABB(r, kUnitMin, kUnitMax, 100.0f, t, n));
}

TEST(RayAABB, RespectsMaxT)
{
	Ray r(glm::vec3(-5, 0, 0), glm::vec3(1, 0, 0));
	float t; glm::vec3 n;
	EXPECT_FALSE(RayAABB(r, kUnitMin, kUnitMax, 2.0f, t, n));
}

// A ray starting inside the box hits at t=0. For line-of-sight this is the
// correct answer — you can't see out of something you're inside — and it
// prevents a guard standing in a crate from seeing through it.
TEST(RayAABB, OriginInsideCountsAsHitAtZero)
{
	Ray r(glm::vec3(0, 0, 0), glm::vec3(1, 0, 0));
	float t; glm::vec3 n;
	ASSERT_TRUE(RayAABB(r, kUnitMin, kUnitMax, 100.0f, t, n));
	EXPECT_NEAR(t, 0.0f, 1e-5f);
}

// Regression: a direction with a zero component makes 1/d infinite, and
// (inf - inf) is NaN, which silently defeats every comparison in the slab
// test. The parallel-slab branch exists to prevent that.
TEST(RayAABB, AxisParallelRayDoesNotProduceNaN)
{
	Ray inside(glm::vec3(-5, 0, 0), glm::vec3(1, 0, 0));   // y and z are exactly 0
	float t; glm::vec3 n;
	EXPECT_TRUE(RayAABB(inside, kUnitMin, kUnitMax, 100.0f, t, n));
	EXPECT_FALSE(std::isnan(t));

	Ray outside(glm::vec3(-5, 9, 0), glm::vec3(1, 0, 0));  // parallel, outside the Y slab
	EXPECT_FALSE(RayAABB(outside, kUnitMin, kUnitMax, 100.0f, t, n));
}

//------------------------------------------------------------------------------
// RayOBB
//------------------------------------------------------------------------------

// The reason OBB exists rather than just fattening a world AABB: a box rotated
// 45 deg about Y reaches ~0.707 at its corner, so a ray at x=0.6 hits the
// rotated box but misses the axis-aligned one. A conservative world AABB would
// report a hit for BOTH, occluding sight lines that are actually clear.
TEST(RayOBB, RotatedBoxIsTestedExactly)
{
	glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0, 1, 0));
	Ray r(glm::vec3(0.6f, 0.0f, 5.0f), glm::vec3(0, 0, -1));
	float t; glm::vec3 n;

	EXPECT_TRUE(RayOBB(r, kUnitMin, kUnitMax, rot, 100.0f, t, n));
	EXPECT_FALSE(RayOBB(r, kUnitMin, kUnitMax, glm::mat4(1.0f), 100.0f, t, n));
}

// The local ray direction is deliberately left unnormalized so that local t
// equals world t. If it were normalized, this would come back as 2.0 (the
// distance in the box's local space) instead of 8.0 world units.
TEST(RayOBB, DistanceStaysInWorldUnitsUnderNonUniformScale)
{
	glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(4, 1, 1));  // spans [-2, 2] in X
	Ray r(glm::vec3(-10, 0, 0), glm::vec3(1, 0, 0));
	float t; glm::vec3 n;

	ASSERT_TRUE(RayOBB(r, kUnitMin, kUnitMax, scale, 100.0f, t, n));
	EXPECT_NEAR(t, 8.0f, 1e-3f);
}

TEST(RayOBB, AccountsForTranslation)
{
	glm::mat4 xform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -20));
	Ray r(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1));
	float t; glm::vec3 n;

	ASSERT_TRUE(RayOBB(r, kUnitMin, kUnitMax, xform, 100.0f, t, n));
	EXPECT_NEAR(t, 19.5f, 1e-3f);
}

//------------------------------------------------------------------------------
// RayTerrain
//------------------------------------------------------------------------------

TEST(RayTerrain, DescendingRayHitsFlatGround)
{
	auto flat = [](float, float) { return 0.0f; };
	Ray r(glm::vec3(0, 10, 0), glm::normalize(glm::vec3(1, -1, 0)));
	float t;

	ASSERT_TRUE(RayTerrain(r, flat, 100.0f, t));
	EXPECT_NEAR(t, 14.1421f, 0.05f);   // 10 units of drop at 45 deg
}

TEST(RayTerrain, LevelRayAboveGroundDoesNotHit)
{
	auto flat = [](float, float) { return 0.0f; };
	Ray r(glm::vec3(0, 5, 0), glm::vec3(1, 0, 0));
	float t;
	EXPECT_FALSE(RayTerrain(r, flat, 100.0f, t));
}

TEST(RayTerrain, UndergroundOriginHitsImmediately)
{
	auto flat = [](float, float) { return 0.0f; };
	Ray r(glm::vec3(0, -1, 0), glm::vec3(1, 0, 0));
	float t;
	ASSERT_TRUE(RayTerrain(r, flat, 100.0f, t));
	EXPECT_NEAR(t, 0.0f, 1e-5f);
}

// The case the stealth game actually cares about: terrain between two points
// must break the sight line, and the same terrain must not break one that
// passes safely over it.
TEST(RayTerrain, RidgeBlocksLowSightLineButNotHighOne)
{
	auto hill = [](float x, float) { return 5.0f - std::fabs(x) * 0.5f; };
	float t;

	Ray low(glm::vec3(-20, 0.5f, 0), glm::vec3(1, 0, 0));
	EXPECT_TRUE(RayTerrain(low, hill, 40.0f, t));

	Ray high(glm::vec3(-20, 30.0f, 0), glm::vec3(1, 0, 0));
	EXPECT_FALSE(RayTerrain(high, hill, 40.0f, t));
}

TEST(RayTerrain, RespectsMaxT)
{
	auto flat = [](float, float) { return 0.0f; };
	Ray r(glm::vec3(0, 10, 0), glm::normalize(glm::vec3(1, -1, 0)));
	float t;
	EXPECT_FALSE(RayTerrain(r, flat, 5.0f, t));   // crossing is at ~14.1
}

//------------------------------------------------------------------------------
// Ray::Between
//------------------------------------------------------------------------------

TEST(RayBetween, ProducesNormalizedDirectionAndLength)
{
	float len = 0.0f;
	Ray r = Ray::Between(glm::vec3(0, 0, 0), glm::vec3(0, 0, -10), len);
	EXPECT_NEAR(len, 10.0f, 1e-4f);
	EXPECT_NEAR(glm::length(r.direction), 1.0f, 1e-4f);
}

// Guard against a divide-by-zero producing a NaN direction that would then
// poison every downstream intersection test.
TEST(RayBetween, DegenerateSameOriginAndTargetIsSafe)
{
	float len = 0.0f;
	Ray r = Ray::Between(glm::vec3(1, 2, 3), glm::vec3(1, 2, 3), len);
	EXPECT_NEAR(len, 0.0f, 1e-6f);
	EXPECT_FALSE(std::isnan(r.direction.x));
	EXPECT_FALSE(std::isnan(r.direction.y));
	EXPECT_FALSE(std::isnan(r.direction.z));
}
