//------------------------------------------------------------------------------
// CapsuleTests.cpp
//
// Capsule / segment / sphere overlap and push-out (Engine/Physics/Capsule.hpp).
//------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "Engine/Physics/Capsule.hpp"

using namespace Nightbloom;

namespace
{
	constexpr float kTol = 1e-4f;

	void ExpectVecNear(const glm::vec3& a, const glm::vec3& b, float tol = kTol)
	{
		EXPECT_NEAR(a.x, b.x, tol);
		EXPECT_NEAR(a.y, b.y, tol);
		EXPECT_NEAR(a.z, b.z, tol);
	}
}

//------------------------------------------------------------------------------
// ClosestPointsSegmentSegment
//------------------------------------------------------------------------------

TEST(SegmentSegment, CrossingSegmentsHaveZeroDistance)
{
	glm::vec3 c1, c2;
	const float d2 = ClosestPointsSegmentSegment(
		{ -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, c1, c2);
	EXPECT_NEAR(d2, 0.0f, kTol);
	ExpectVecNear(c1, { 0, 0, 0 });
	ExpectVecNear(c2, { 0, 0, 0 });
}

TEST(SegmentSegment, SkewSegmentsClosestInInteriors)
{
	// X axis at y=0, and a Z-direction segment hovering at y=2 over x=0.5.
	glm::vec3 c1, c2;
	const float d2 = ClosestPointsSegmentSegment(
		{ -1, 0, 0 }, { 1, 0, 0 }, { 0.5f, 2, -1 }, { 0.5f, 2, 1 }, c1, c2);
	EXPECT_NEAR(d2, 4.0f, kTol);
	ExpectVecNear(c1, { 0.5f, 0, 0 });
	ExpectVecNear(c2, { 0.5f, 2, 0 });
}

TEST(SegmentSegment, ClosestAtEndpointsClamps)
{
	// Collinear, end-to-end with a 1 m gap: the answer is the two inner ends.
	glm::vec3 c1, c2;
	const float d2 = ClosestPointsSegmentSegment(
		{ 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 }, { 3, 0, 0 }, c1, c2);
	EXPECT_NEAR(d2, 1.0f, kTol);
	ExpectVecNear(c1, { 1, 0, 0 });
	ExpectVecNear(c2, { 2, 0, 0 });
}

TEST(SegmentSegment, ParallelOverlappingSegments)
{
	glm::vec3 c1, c2;
	const float d2 = ClosestPointsSegmentSegment(
		{ 0, 0, 0 }, { 2, 0, 0 }, { 1, 3, 0 }, { 4, 3, 0 }, c1, c2);
	EXPECT_NEAR(d2, 9.0f, kTol);
	EXPECT_NEAR(c1.y, 0.0f, kTol);
	EXPECT_NEAR(c2.y, 3.0f, kTol);
}

TEST(SegmentSegment, DegeneratePoints)
{
	glm::vec3 c1, c2;
	// Point vs point.
	EXPECT_NEAR(ClosestPointsSegmentSegment({ 0, 0, 0 }, { 0, 0, 0 }, { 3, 4, 0 }, { 3, 4, 0 }, c1, c2), 25.0f, kTol);
	// Point vs segment.
	EXPECT_NEAR(ClosestPointsSegmentSegment({ 1, 5, 0 }, { 1, 5, 0 }, { 0, 0, 0 }, { 2, 0, 0 }, c1, c2), 25.0f, kTol);
	ExpectVecNear(c2, { 1, 0, 0 });
	// Segment vs point.
	EXPECT_NEAR(ClosestPointsSegmentSegment({ 0, 0, 0 }, { 2, 0, 0 }, { 1, 5, 0 }, { 1, 5, 0 }, c1, c2), 25.0f, kTol);
	ExpectVecNear(c1, { 1, 0, 0 });
}

TEST(SegmentSegment, ClosestPointOnSegmentClamps)
{
	ExpectVecNear(ClosestPointOnSegment({ -5, 1, 0 }, { 0, 0, 0 }, { 2, 0, 0 }), { 0, 0, 0 });
	ExpectVecNear(ClosestPointOnSegment({ 1, 1, 0 }, { 0, 0, 0 }, { 2, 0, 0 }), { 1, 0, 0 });
	ExpectVecNear(ClosestPointOnSegment({ 9, 1, 0 }, { 0, 0, 0 }, { 2, 0, 0 }), { 2, 0, 0 });
}

//------------------------------------------------------------------------------
// Capsule construction
//------------------------------------------------------------------------------

TEST(CapsuleShape, UprightSpansFeetToHead)
{
	const Capsule c = Capsule::Upright({ 0, 0, 0 }, 0.3f, 1.8f);
	EXPECT_NEAR(c.a.y - c.radius, 0.0f, kTol);   // bottom of the cap on the feet
	EXPECT_NEAR(c.b.y + c.radius, 1.8f, kTol);   // top of the cap at full height
}

TEST(CapsuleShape, UprightShorterThanDiameterIsASphere)
{
	const Capsule c = Capsule::Upright({ 0, 0, 0 }, 0.5f, 0.6f);
	ExpectVecNear(c.a, c.b);
}

//------------------------------------------------------------------------------
// Overlap
//------------------------------------------------------------------------------

TEST(CapsuleOverlap, SideBySideUprightBodies)
{
	const Capsule p1 = Capsule::Upright({ 0, 0, 0 }, 0.3f, 1.8f);
	EXPECT_TRUE(OverlapCapsules(p1, Capsule::Upright({ 0.5f, 0, 0 }, 0.3f, 1.8f)));
	EXPECT_FALSE(OverlapCapsules(p1, Capsule::Upright({ 0.7f, 0, 0 }, 0.3f, 1.8f)));
}

TEST(CapsuleOverlap, TouchingIsNotOverlapping)
{
	// Exactly r1 + r2 apart: a hit should need real penetration.
	EXPECT_FALSE(OverlapCapsules(Capsule::Sphere({ 0, 0, 0 }, 0.5f), Capsule::Sphere({ 1, 0, 0 }, 0.5f)));
}

TEST(CapsuleOverlap, FistReachesTorsoOnlyWithinRange)
{
	// A punch: fist sphere at the end of an arm vs an upright torso capsule.
	const Capsule torso = Capsule::Upright({ 1.0f, 0, 0 }, 0.3f, 1.8f);
	EXPECT_TRUE(OverlapSphereCapsule({ 0.8f, 1.4f, 0 }, 0.1f, torso));
	EXPECT_FALSE(OverlapSphereCapsule({ 0.5f, 1.4f, 0 }, 0.1f, torso));
	// Over the head: misses even horizontally aligned with the torso.
	EXPECT_FALSE(OverlapSphereCapsule({ 1.0f, 2.2f, 0 }, 0.1f, torso));
}

TEST(CapsuleOverlap, LowKickHitsShinNotHead)
{
	const Capsule body = Capsule::Upright({ 0, 0, 0 }, 0.3f, 1.8f);
	const Capsule kick{ { -1.0f, 0.2f, 0 }, { -0.2f, 0.2f, 0 }, 0.12f };
	EXPECT_TRUE(OverlapCapsules(kick, body));
	const Capsule shortKick{ { -1.0f, 0.2f, 0 }, { -0.5f, 0.2f, 0 }, 0.12f };
	EXPECT_FALSE(OverlapCapsules(shortKick, body));
}

TEST(CapsuleOverlap, SpheresMatchCapsulePath)
{
	EXPECT_TRUE(OverlapSpheres({ 0, 0, 0 }, 1.0f, { 1.5f, 0, 0 }, 1.0f));
	EXPECT_FALSE(OverlapSpheres({ 0, 0, 0 }, 1.0f, { 2.5f, 0, 0 }, 1.0f));
}

//------------------------------------------------------------------------------
// Push-out (pushboxes)
//------------------------------------------------------------------------------

TEST(CapsuleResolve, NoContactWhenApart)
{
	const CapsuleContact c = ResolveCapsules(Capsule::Sphere({ 0, 0, 0 }, 0.5f), Capsule::Sphere({ 2, 0, 0 }, 0.5f));
	EXPECT_FALSE(c.hit);
}

TEST(CapsuleResolve, PushSeparatesExactly)
{
	const Capsule x = Capsule::Upright({ 0.4f, 0, 0 }, 0.3f, 1.8f);
	const Capsule y = Capsule::Upright({ 0.0f, 0, 0 }, 0.3f, 1.8f);
	const CapsuleContact c = ResolveCapsules(x, y);
	ASSERT_TRUE(c.hit);
	EXPECT_NEAR(c.depth, 0.2f, kTol);
	ExpectVecNear(c.normal, { 1, 0, 0 });   // from Y towards X
	ExpectVecNear(c.push, { 0.2f, 0, 0 });

	// Applying the push leaves them exactly touching — no longer overlapping.
	Capsule moved = x;
	moved.a += c.push; moved.b += c.push;
	EXPECT_FALSE(OverlapCapsules(moved, y));
	EXPECT_FALSE(ResolveCapsules(moved, y).hit);
}

TEST(CapsuleResolve, ContactPointIsBetweenSurfaces)
{
	const CapsuleContact c = ResolveCapsules(Capsule::Sphere({ 0.8f, 0, 0 }, 0.5f), Capsule::Sphere({ 0, 0, 0 }, 0.5f));
	ASSERT_TRUE(c.hit);
	// X's facing surface at x=0.3, Y's at x=0.5 → midpoint 0.4.
	ExpectVecNear(c.point, { 0.4f, 0, 0 });
}

TEST(CapsuleResolve, CoincidentUprightBodiesSeparateHorizontally)
{
	// Two fighters dashed into the exact same spot: segments coincide, so there
	// is no closest-point direction. They must still separate, on the ground plane.
	const Capsule x = Capsule::Upright({ 0, 0, 0 }, 0.3f, 1.8f);
	const CapsuleContact c = ResolveCapsules(x, x);
	ASSERT_TRUE(c.hit);
	EXPECT_NEAR(c.normal.y, 0.0f, kTol);
	EXPECT_NEAR(glm::length(c.normal), 1.0f, kTol);
	EXPECT_NEAR(c.depth, 0.6f, kTol);
}

TEST(CapsuleResolve, SegmentsCrossingUsesMidpointDirection)
{
	// An upright body and a horizontal capsule crossing through it, offset in Z.
	const Capsule body = Capsule::Upright({ 0, 0, 0 }, 0.3f, 1.8f);
	const Capsule bar{ { -1, 1, 0 }, { 1, 1, 0 }, 0.2f };
	const CapsuleContact c = ResolveCapsules(bar, body);
	ASSERT_TRUE(c.hit);
	EXPECT_NEAR(glm::length(c.normal), 1.0f, kTol);
	EXPECT_NEAR(c.depth, 0.5f, kTol);
}
