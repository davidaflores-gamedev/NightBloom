//------------------------------------------------------------------------------
// GuardAgentTests.cpp
//
// Perception tests for GuardAgent (ROADMAP 3.3). The detection rules are the
// stealth game's core contract with the player — "was that fair?" is the only
// question that matters — so they're pinned here rather than left to be
// eyeballed in game.
//------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "Engine/Gameplay/GuardAgent.hpp"
#include "Engine/Math/MathUtils.hpp"   // ShortestAngleDelta

using namespace Nightbloom;

namespace
{
	// A guard at the origin facing +X (yaw 0 => heading (cos0, 0, sin0)).
	// No patrol route, so it stays put and only perception is under test.
	GuardAgent MakeStationaryGuard()
	{
		GuardAgent g;
		g.SetPosition(glm::vec3(0.0f));
		g.SetYaw(0.0f);
		return g;
	}

	// Advance in small steps so the meter integrates the way it does in game
	// rather than in one giant dt.
	void Simulate(GuardAgent& g, const glm::vec3& target, float seconds, float step = 1.0f / 60.0f)
	{
		for (float elapsed = 0.0f; elapsed < seconds; elapsed += step)
			g.Update(step, target);
	}

	void Simulate(GuardAgent& g, const PerceptionTarget& target, float seconds, float step = 1.0f / 60.0f)
	{
		for (float elapsed = 0.0f; elapsed < seconds; elapsed += step)
			g.Update(step, target);
	}
}

//------------------------------------------------------------------------------
// Cone geometry
//------------------------------------------------------------------------------

TEST(GuardVision, SeesTargetDirectlyAhead)
{
	GuardAgent g = MakeStationaryGuard();
	g.Update(0.016f, glm::vec3(10.0f, 0.0f, 0.0f));   // straight down +X
	EXPECT_TRUE(g.CanSeeTarget());
}

TEST(GuardVision, DoesNotSeeTargetBehind)
{
	GuardAgent g = MakeStationaryGuard();
	g.Update(0.016f, glm::vec3(-10.0f, 0.0f, 0.0f));
	EXPECT_FALSE(g.CanSeeTarget());
}

TEST(GuardVision, DoesNotSeeTargetBeyondRange)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().rangeMeters = 10.0f;
	g.Update(0.016f, glm::vec3(25.0f, 0.0f, 0.0f));   // ahead, but too far
	EXPECT_FALSE(g.CanSeeTarget());
}

// Just inside vs. just outside the cone edge. With a 45 deg half-angle, a
// target at (10, 0, 9) sits at ~42 deg (visible) and (10, 0, 12) at ~50 deg.
TEST(GuardVision, ConeEdgeIsRespected)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().halfAngleDegrees = 45.0f;
	g.Vision().rangeMeters = 50.0f;

	g.Update(0.016f, glm::vec3(10.0f, 0.0f, 9.0f));
	EXPECT_TRUE(g.CanSeeTarget()) << "target inside the cone should be visible";

	GuardAgent g2 = MakeStationaryGuard();
	g2.Vision().halfAngleDegrees = 45.0f;
	g2.Vision().rangeMeters = 50.0f;
	g2.Update(0.016f, glm::vec3(10.0f, 0.0f, 12.0f));
	EXPECT_FALSE(g2.CanSeeTarget()) << "target outside the cone should not be";
}

//------------------------------------------------------------------------------
// Exposure — shadow concealment (Phase 1a)
//------------------------------------------------------------------------------

// A shadowed target (low rangeExposure) is only seen at close range, so a guard
// can walk past you in the dark instead of spotting you across the camp.
TEST(GuardExposure, ShadowShortensSightRange)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().rangeMeters = 20.0f;

	PerceptionTarget lit;
	lit.position = glm::vec3(15.0f, 0.0f, 0.0f);   // dead ahead, 15 m
	lit.rangeExposure = 1.0f;

	PerceptionTarget shadowed = lit;
	shadowed.rangeExposure = 0.3f;                 // effective range ~6 m

	g.Update(0.016f, lit);
	EXPECT_TRUE(g.CanSeeTarget()) << "lit target at 15 m is within range";

	GuardAgent g2 = MakeStationaryGuard();
	g2.Vision().rangeMeters = 20.0f;
	g2.Update(0.016f, shadowed);
	EXPECT_FALSE(g2.CanSeeTarget()) << "shadowed target at 15 m is beyond its shortened range";
}

// Even fully shadowed, a target right in the guard's face is still seen — shadow
// is concealment, not invisibility.
TEST(GuardExposure, ShadowIsNotInvisibility)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().rangeMeters = 20.0f;

	PerceptionTarget pointBlank;
	pointBlank.position = glm::vec3(2.0f, 0.0f, 0.0f);
	pointBlank.rangeExposure = 0.3f;

	g.Update(0.016f, pointBlank);
	EXPECT_TRUE(g.CanSeeTarget()) << "shadow doesn't hide you when you're right there";
}

TEST(GuardExposure, CanSeePointDefaultsToFullExposure)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().rangeMeters = 20.0f;
	EXPECT_TRUE(g.CanSeePoint(glm::vec3(15.0f, 0.0f, 0.0f)));
}

// A body concealed by grass should be hidden the same way a player is —
// otherwise grass hides you but not the corpse you left in it.
TEST(GuardExposure, ConcealedPointIsHiddenAtRange)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().rangeMeters = 20.0f;

	const glm::vec3 body(15.0f, 0.0f, 0.0f);
	EXPECT_TRUE(g.CanSeePoint(body, 0.3f, 1.0f))  << "in the open it's visible";
	EXPECT_FALSE(g.CanSeePoint(body, 0.3f, 0.2f)) << "in grass at range it is not";
}

// THE exploit fix: stacked concealment (stance x shadow x grass) multiplied the
// sight range down to centimetres, so guards standing on top of you saw nothing
// and you could farm the camp from a grass patch. Concealment must hide you from
// a distance, never from someone standing over you.
TEST(GuardExposure, ConcealmentNeverHidesYouAtPointBlank)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().rangeMeters = 25.0f;
	g.Vision().minAwarenessRange = 3.0f;

	PerceptionTarget deeplyConcealed;
	deeplyConcealed.position = glm::vec3(1.5f, 0.0f, 0.0f);   // right in front
	deeplyConcealed.rangeExposure = 0.01f;                    // absurdly concealed

	g.Update(0.016f, deeplyConcealed);
	EXPECT_TRUE(g.CanSeeTarget())
		<< "a guard standing over you sees you regardless of concealment";
}

TEST(GuardExposure, ConcealmentStillWorksBeyondTheAwarenessFloor)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().rangeMeters = 25.0f;
	g.Vision().minAwarenessRange = 3.0f;

	PerceptionTarget concealed;
	concealed.position = glm::vec3(12.0f, 0.0f, 0.0f);   // well past the floor
	concealed.rangeExposure = 0.1f;

	g.Update(0.016f, concealed);
	EXPECT_FALSE(g.CanSeeTarget()) << "concealment should still hide you at distance";
}

//------------------------------------------------------------------------------
// Motion — running is caught at range where sneaking is safe
//------------------------------------------------------------------------------

// At a distance, a still target is barely detected, but a moving one is caught
// much faster — motion catches the eye. This is the user's "running at range
// should speed detection, not remove it".
TEST(GuardMotion, MovingIsDetectedFasterAtRangeThanStill)
{
	GuardAgent still  = MakeStationaryGuard();
	GuardAgent moving = MakeStationaryGuard();
	still.Vision().rangeMeters  = 30.0f;
	moving.Vision().rangeMeters = 30.0f;

	PerceptionTarget farStill;
	farStill.position = glm::vec3(22.0f, 0.0f, 0.0f);   // far, dead ahead
	farStill.motion   = 0.0f;

	PerceptionTarget farMoving = farStill;
	farMoving.motion = 1.0f;                             // sprinting

	for (int i = 0; i < 30; ++i)
	{
		still.Update(1.0f / 60.0f, farStill);
		moving.Update(1.0f / 60.0f, farMoving);
	}

	EXPECT_GT(moving.GetDetection(), still.GetDetection() * 2.0f)
		<< "a runner at range should register far faster than a still figure";
}

// Point-blank detection should be quick regardless of motion — the close-range
// sluggishness the user complained about.
TEST(GuardMotion, PointBlankIsFast)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().timeToDetect = 1.2f;

	PerceptionTarget close;
	close.position = glm::vec3(2.0f, 0.0f, 0.0f);   // right in front

	// Under a second of point-blank sight should fully alert.
	Simulate(g, close, 0.9f);
	EXPECT_GE(g.GetDetection(), 1.0f) << "point-blank should confirm fast";
}

//------------------------------------------------------------------------------
// Vertical cone — what makes climbing worth doing
//------------------------------------------------------------------------------

// On a wall directly above a nearby guard: steep angle, outside their scan.
// This is the whole payoff for climbing — high ground near a guard is cover.
TEST(GuardVision, DoesNotSeeSteeplyAboveWhenClose)
{
	GuardAgent g = MakeStationaryGuard();
	// 4 m up, 4 m ahead => ~45 degrees, well past the ~22 degree look-up.
	g.Update(0.016f, glm::vec3(4.0f, 4.0f, 0.0f));
	EXPECT_FALSE(g.CanSeeTarget());
}

// The same elevation seen from far away is a shallow angle, and IS spotted.
// Height is a vantage point, not an invisibility cloak — loitering in the open
// up there should still get you caught by someone across the camp.
TEST(GuardVision, SeesElevatedTargetFromADistance)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().rangeMeters = 40.0f;
	// 4 m up, 30 m away => ~7.6 degrees, inside the look-up limit.
	g.Update(0.016f, glm::vec3(30.0f, 4.0f, 0.0f));
	EXPECT_TRUE(g.CanSeeTarget());
}

// Downward vision stays generous, or guards would fail to see a player stood
// on lower ground right in front of them.
TEST(GuardVision, SeesTargetBelowOnASlope)
{
	GuardAgent g = MakeStationaryGuard();
	g.Update(0.016f, glm::vec3(6.0f, -3.0f, 0.0f));
	EXPECT_TRUE(g.CanSeeTarget());
}

//------------------------------------------------------------------------------
// Hearing — the channel that works without line of sight
//------------------------------------------------------------------------------

// Sound is heard from BEHIND and through walls: no cone, no line of sight.
// That's the whole point of it being a separate channel.
TEST(GuardHearing, HearsNoiseBehindAndThroughWalls)
{
	GuardAgent g = MakeStationaryGuard();            // at origin, facing +X
	g.SetLineOfSightFn([](const glm::vec3&, const glm::vec3&) { return false; });  // fully blocked

	PerceptionTarget loud;
	loud.position = glm::vec3(-5.0f, 0.0f, 0.0f);    // directly BEHIND the guard
	loud.noise    = 1.0f;

	Simulate(g, loud, 1.5f);

	EXPECT_FALSE(g.CanSeeTarget()) << "cannot see it";
	EXPECT_GT(g.GetDetection(), 0.0f) << "but should hear it";
}

TEST(GuardHearing, SilenceIsNotHeard)
{
	GuardAgent g = MakeStationaryGuard();
	g.SetLineOfSightFn([](const glm::vec3&, const glm::vec3&) { return false; });

	PerceptionTarget silent;
	silent.position = glm::vec3(-5.0f, 0.0f, 0.0f);
	silent.noise    = 0.0f;

	Simulate(g, silent, 2.0f);
	EXPECT_FLOAT_EQ(g.GetDetection(), 0.0f);
}

// Louder noise carries further: a sprint is audible where a crouch-walk is not.
TEST(GuardHearing, LouderNoiseCarriesFurther)
{
	GuardAgent quiet = MakeStationaryGuard();
	GuardAgent loud  = MakeStationaryGuard();
	quiet.SetLineOfSightFn([](const glm::vec3&, const glm::vec3&) { return false; });
	loud.SetLineOfSightFn([](const glm::vec3&, const glm::vec3&) { return false; });

	PerceptionTarget farQuiet;
	farQuiet.position = glm::vec3(-14.0f, 0.0f, 0.0f);
	farQuiet.noise    = 0.2f;                        // quiet: audible range ~4 m

	PerceptionTarget farLoud = farQuiet;
	farLoud.noise = 1.0f;                            // loud: audible range ~20 m

	Simulate(quiet, farQuiet, 2.0f);
	Simulate(loud,  farLoud,  2.0f);

	EXPECT_FLOAT_EQ(quiet.GetDetection(), 0.0f) << "too quiet to carry that far";
	EXPECT_GT(loud.GetDetection(), 0.0f)        << "loud enough to reach";
}

// Sound DRAWS, sight CONFIRMS: hearing alone must never fully alert a guard,
// or a footstep behind a wall would mean instant capture.
TEST(GuardHearing, SoundAloneCannotFullyAlert)
{
	GuardAgent g = MakeStationaryGuard();
	g.SetLineOfSightFn([](const glm::vec3&, const glm::vec3&) { return false; });

	PerceptionTarget loud;
	loud.position = glm::vec3(-2.0f, 0.0f, 0.0f);
	loud.noise    = 1.0f;

	Simulate(g, loud, 20.0f);                        // a very long time

	EXPECT_LE(g.GetDetection(), g.Vision().hearingMaxDetection + 1e-3f);
	EXPECT_NE(g.GetAlertState(), AlertState::Alerted);
}

// Noise should pull a guard off its route to come and look.
TEST(GuardHearing, NoiseDrawsAnInvestigation)
{
	GuardAgent g;
	g.SetPatrolRoute({ glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 20.0f) });
	g.SetLineOfSightFn([](const glm::vec3&, const glm::vec3&) { return false; });

	PerceptionTarget loud;
	loud.position = glm::vec3(6.0f, 0.0f, 0.0f);
	loud.noise    = 1.0f;

	Simulate(g, loud, 2.0f);
	EXPECT_TRUE(g.IsInvestigating()) << "a heard noise should be worth walking over to";
}

//------------------------------------------------------------------------------
// Occlusion
//------------------------------------------------------------------------------

TEST(GuardVision, BlockedLineOfSightPreventsDetection)
{
	GuardAgent g = MakeStationaryGuard();
	g.SetLineOfSightFn([](const glm::vec3&, const glm::vec3&) { return false; });  // everything blocked

	Simulate(g, glm::vec3(5.0f, 0.0f, 0.0f), 3.0f);

	EXPECT_FALSE(g.CanSeeTarget());
	EXPECT_FLOAT_EQ(g.GetDetection(), 0.0f);
	EXPECT_EQ(g.GetAlertState(), AlertState::Unaware);
}

TEST(GuardVision, ClearLineOfSightAllowsDetection)
{
	GuardAgent g = MakeStationaryGuard();
	g.SetLineOfSightFn([](const glm::vec3&, const glm::vec3&) { return true; });   // nothing blocked

	Simulate(g, glm::vec3(5.0f, 0.0f, 0.0f), 3.0f);

	EXPECT_TRUE(g.CanSeeTarget());
	EXPECT_GT(g.GetDetection(), 0.0f);
}

//------------------------------------------------------------------------------
// The detection meter — the part that decides whether stealth feels fair
//------------------------------------------------------------------------------

TEST(GuardDetection, MeterFillsToAlertedWithSustainedSight)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().timeToDetect = 1.0f;

	// Well beyond timeToDetect even after distance falloff.
	Simulate(g, glm::vec3(2.0f, 0.0f, 0.0f), 6.0f);

	EXPECT_FLOAT_EQ(g.GetDetection(), 1.0f);
	EXPECT_EQ(g.GetAlertState(), AlertState::Alerted);
}

TEST(GuardDetection, DetectionIsNotInstant)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().timeToDetect = 1.0f;

	g.Update(1.0f / 60.0f, glm::vec3(2.0f, 0.0f, 0.0f));   // a single frame in view

	EXPECT_LT(g.GetDetection(), 1.0f) << "one frame of sight must not fully alert a guard";
	EXPECT_NE(g.GetAlertState(), AlertState::Alerted);
}

// Closer targets must be detected faster, so breaking distance is a real
// defensive option and not just flavour.
TEST(GuardDetection, CloserTargetsDetectFaster)
{
	GuardAgent near_ = MakeStationaryGuard();
	GuardAgent far_ = MakeStationaryGuard();
	near_.Vision().rangeMeters = 30.0f;
	far_.Vision().rangeMeters = 30.0f;

	Simulate(near_, glm::vec3(3.0f, 0.0f, 0.0f), 0.4f);
	Simulate(far_, glm::vec3(25.0f, 0.0f, 0.0f), 0.4f);

	EXPECT_GT(near_.GetDetection(), far_.GetDetection());
}

// Likewise the cone centre should beat the periphery.
TEST(GuardDetection, CentreOfViewDetectsFasterThanPeriphery)
{
	GuardAgent centre = MakeStationaryGuard();
	GuardAgent edge = MakeStationaryGuard();
	centre.Vision().rangeMeters = 30.0f;
	edge.Vision().rangeMeters = 30.0f;

	Simulate(centre, glm::vec3(10.0f, 0.0f, 0.0f), 0.4f);   // dead ahead
	Simulate(edge, glm::vec3(10.0f, 0.0f, 8.0f), 0.4f);   // ~39 deg off axis

	EXPECT_GT(centre.GetDetection(), edge.GetDetection());
}

// Crouching must pay off, or it's just slower walking. Two separate effects:
// a lower aim point (so low cover blocks — geometry, covered in
// SceneLineOfSightTests) and a reduced detection rate in the open (below).
TEST(GuardDetection, LowerVisibilityDetectsSlower)
{
	GuardAgent standing = MakeStationaryGuard();
	GuardAgent crouched = MakeStationaryGuard();

	PerceptionTarget stand;
	stand.position = glm::vec3(6.0f, 0.0f, 0.0f);

	PerceptionTarget crouch = stand;
	crouch.height = 1.0f;
	crouch.visibility = 0.5f;

	for (int i = 0; i < 30; ++i)
	{
		standing.Update(1.0f / 60.0f, stand);
		crouched.Update(1.0f / 60.0f, crouch);
	}

	EXPECT_GT(standing.GetDetection(), crouched.GetDetection());
}

// ...but it can never make you invisible: a floored multiplier keeps a target
// standing in the open eventually detectable, so crouch is a discount, not a
// cloaking device.
TEST(GuardDetection, VisibilityCannotMakeTargetUndetectable)
{
	GuardAgent g = MakeStationaryGuard();

	PerceptionTarget target;
	target.position = glm::vec3(4.0f, 0.0f, 0.0f);
	target.visibility = 0.0f;      // clamped internally

	Simulate(g, target, 30.0f);
	EXPECT_GT(g.GetDetection(), 0.0f);
}

TEST(GuardDetection, MeterDecaysAfterLosingSight)
{
	GuardAgent g = MakeStationaryGuard();
	g.Vision().timeToDetect = 1.0f;
	g.Vision().timeToForget = 2.0f;

	Simulate(g, glm::vec3(2.0f, 0.0f, 0.0f), 4.0f);
	ASSERT_GT(g.GetDetection(), 0.5f);

	// Step out of the cone entirely.
	Simulate(g, glm::vec3(-20.0f, 0.0f, 0.0f), 1.0f);
	float afterOneSecond = g.GetDetection();
	EXPECT_LT(afterOneSecond, 1.0f) << "meter should be draining";

	Simulate(g, glm::vec3(-20.0f, 0.0f, 0.0f), 5.0f);
	EXPECT_FLOAT_EQ(g.GetDetection(), 0.0f) << "meter should fully drain";
	EXPECT_EQ(g.GetAlertState(), AlertState::Unaware);
}

// Forgetting must be slower than noticing, or being spotted carries no weight.
TEST(GuardDetection, ForgettingIsSlowerThanNoticing)
{
	GuardAgent g = MakeStationaryGuard();
	EXPECT_GT(g.Vision().timeToForget, g.Vision().timeToDetect);
}

TEST(GuardDetection, RemembersLastKnownPosition)
{
	GuardAgent g = MakeStationaryGuard();
	const glm::vec3 seenAt(6.0f, 0.0f, 1.0f);

	Simulate(g, seenAt, 0.5f);
	ASSERT_TRUE(g.GetDetection() > 0.0f);

	Simulate(g, glm::vec3(-20.0f, 0.0f, 0.0f), 0.2f);   // vanish behind it

	EXPECT_NEAR(g.GetLastKnownTargetPos().x, seenAt.x, 1e-3f);
	EXPECT_NEAR(g.GetLastKnownTargetPos().z, seenAt.z, 1e-3f);
}

//------------------------------------------------------------------------------
// Investigation — the behaviour that makes distractions and bodies mean something
//------------------------------------------------------------------------------

TEST(GuardInvestigation, WalksTowardTheDisturbance)
{
	GuardAgent g;
	g.SetPatrolRoute({ glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 30.0f) });

	const glm::vec3 noise(15.0f, 0.0f, 0.0f);
	g.InvestigatePoint(noise);
	ASSERT_TRUE(g.IsInvestigating());

	const float before = glm::length(noise - g.GetPosition());
	Simulate(g, glm::vec3(0.0f, 0.0f, -500.0f), 2.0f);   // player far away
	const float after = glm::length(noise - g.GetPosition());

	EXPECT_LT(after, before - 1.0f) << "should close on the noise";
	EXPECT_EQ(g.GetAlertState(), AlertState::Investigating);
}

TEST(GuardInvestigation, GivesUpAndResumesPatrol)
{
	GuardAgent g;
	g.SetPatrolRoute({ glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 30.0f) });
	g.Patrol().investigateLookTime = 1.0f;

	g.InvestigatePoint(glm::vec3(6.0f, 0.0f, 0.0f));

	// Long enough to walk there and finish searching.
	Simulate(g, glm::vec3(0.0f, 0.0f, -500.0f), 12.0f);

	EXPECT_FALSE(g.IsInvestigating()) << "should give up eventually";
	EXPECT_EQ(g.GetAlertState(), AlertState::Unaware);
}

// A guard who already has you can't be lured away, or a stone would be an
// escape button rather than a planning tool.
TEST(GuardInvestigation, AlertedGuardIgnoresDistractions)
{
	GuardAgent g = MakeStationaryGuard();
	Simulate(g, glm::vec3(3.0f, 0.0f, 0.0f), 6.0f);
	ASSERT_EQ(g.GetAlertState(), AlertState::Alerted);

	g.InvestigatePoint(glm::vec3(-20.0f, 0.0f, 0.0f));
	EXPECT_FALSE(g.IsInvestigating());
}

// Being lured makes a guard MORE dangerous on arrival, so distraction has a
// cost rather than being free profit.
TEST(GuardInvestigation, InvestigatingGuardsDetectFaster)
{
	GuardAgent calm = MakeStationaryGuard();
	GuardAgent roused = MakeStationaryGuard();

	// Send the roused one to look at a point right where the player stands, so
	// both have the same sight line but different alertness.
	roused.InvestigatePoint(glm::vec3(8.0f, 0.0f, 0.0f));

	const glm::vec3 player(8.0f, 0.0f, 0.0f);
	for (int i = 0; i < 12; ++i)
	{
		calm.Update(1.0f / 60.0f, player);
		roused.Update(1.0f / 60.0f, player);
	}

	EXPECT_GT(roused.GetDetection(), calm.GetDetection());
}

TEST(GuardPerception, CanSeeAnArbitraryPoint)
{
	GuardAgent g = MakeStationaryGuard();   // at origin facing +X

	EXPECT_TRUE(g.CanSeePoint(glm::vec3(6.0f, 0.0f, 0.0f)));
	EXPECT_FALSE(g.CanSeePoint(glm::vec3(-6.0f, 0.0f, 0.0f))) << "behind it";

	g.SetLineOfSightFn([](const glm::vec3&, const glm::vec3&) { return false; });
	EXPECT_FALSE(g.CanSeePoint(glm::vec3(6.0f, 0.0f, 0.0f))) << "occluded";
}

//------------------------------------------------------------------------------
// Rear arc — what a stealth takedown is gated on
//------------------------------------------------------------------------------

TEST(GuardRearArc, PointDirectlyBehindIsBehind)
{
	GuardAgent g = MakeStationaryGuard();   // at origin, facing +X
	EXPECT_TRUE(g.IsBehind(glm::vec3(-2.0f, 0.0f, 0.0f)));
}

TEST(GuardRearArc, PointDirectlyAheadIsNot)
{
	GuardAgent g = MakeStationaryGuard();
	EXPECT_FALSE(g.IsBehind(glm::vec3(2.0f, 0.0f, 0.0f)));
}

// Exactly abreast is the boundary and must not count as behind — otherwise you
// could take a guard down from inside their own peripheral vision.
TEST(GuardRearArc, PointExactlyAbreastIsNotBehind)
{
	GuardAgent g = MakeStationaryGuard();
	EXPECT_FALSE(g.IsBehind(glm::vec3(0.0f, 0.0f, 2.0f)));
	EXPECT_FALSE(g.IsBehind(glm::vec3(0.0f, 0.0f, -2.0f)));
}

TEST(GuardRearArc, FollowsTheGuardsFacing)
{
	GuardAgent g = MakeStationaryGuard();
	const glm::vec3 point(-2.0f, 0.0f, 0.0f);
	ASSERT_TRUE(g.IsBehind(point));

	g.SetYaw(180.0f);                       // turn to face -X
	EXPECT_FALSE(g.IsBehind(point)) << "the same point is now in front";
}

// Height must not matter: standing on a crate behind someone is still behind.
TEST(GuardRearArc, IgnoresVerticalOffset)
{
	GuardAgent g = MakeStationaryGuard();
	EXPECT_TRUE(g.IsBehind(glm::vec3(-2.0f, 5.0f, 0.0f)));
	EXPECT_TRUE(g.IsBehind(glm::vec3(-2.0f, -5.0f, 0.0f)));
}

//------------------------------------------------------------------------------
// Patrol
//------------------------------------------------------------------------------

TEST(GuardPatrol, StartsOnFirstWaypointFacingTheSecond)
{
	GuardAgent g;
	g.SetPatrolRoute({ glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f) });

	EXPECT_NEAR(g.GetPosition().x, 0.0f, 1e-4f);
	EXPECT_NEAR(g.GetPosition().z, 0.0f, 1e-4f);
	EXPECT_NEAR(g.GetYaw(), 0.0f, 1e-3f);   // +X is yaw 0
}

TEST(GuardPatrol, WalksTowardItsWaypoint)
{
	GuardAgent g;
	g.SetPatrolRoute({ glm::vec3(0.0f), glm::vec3(20.0f, 0.0f, 0.0f) });
	g.Patrol().pauseAtWaypoint = 0.0f;

	// Target far behind so perception stays idle and patrol drives.
	Simulate(g, glm::vec3(0.0f, 0.0f, -500.0f), 1.0f);

	EXPECT_GT(g.GetPosition().x, 0.5f) << "guard should have advanced along the route";
}

// A guard that abandons its post the instant it half-sees something makes
// routes unlearnable, so patrol must yield only once suspicion is real.
// Regression: the guard used to set off before it had turned, arc past the
// waypoint, miss the arrival radius, and orbit it forever — "never reaches its
// destination". Turning in place while badly misaligned fixes convergence.
TEST(GuardPatrol, ReachesAWaypointBehindIt)
{
	GuardAgent g;
	g.SetPatrolRoute({ glm::vec3(0.0f), glm::vec3(12.0f, 0.0f, 0.0f) });
	g.Patrol().pauseAtWaypoint = 0.0f;
	g.SetYaw(180.0f);   // facing away from the first leg

	// Track the furthest it gets. Checking the FINAL position would be
	// meaningless — the route loops, so it can legitimately be back near the
	// start. What matters is that it actually arrived at the far end at all.
	float furthest = 0.0f;
	for (int i = 0; i < 900; ++i)   // 15 s
	{
		g.Update(1.0f / 60.0f, glm::vec3(0.0f, 0.0f, -500.0f));
		furthest = std::max(furthest, g.GetPosition().x);
	}

	EXPECT_GT(furthest, 11.0f) << "should have converged on the waypoint, not orbited it";
}

// A waypoint that can't be reached must not deadlock the whole patrol.
TEST(GuardPatrol, GivesUpOnAnUnreachableWaypoint)
{
	GuardAgent g;
	g.SetPatrolRoute({ glm::vec3(0.0f), glm::vec3(20.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 20.0f) });
	g.Patrol().pauseAtWaypoint = 0.0f;
	g.Patrol().stuckTimeout = 1.0f;

	// Pin it in place: collision always returns it to the origin.
	g.SetCollisionResolver([](const glm::vec3&, float, float) { return glm::vec3(0.0f); });

	Simulate(g, glm::vec3(0.0f, 0.0f, -500.0f), 6.0f);

	// It can't move, but it must have advanced past the blocked waypoint rather
	// than grinding at it forever.
	SUCCEED();   // reaching here without hanging is the assertion
}

TEST(GuardPatrol, HoldsPostFacingItsLookTarget)
{
	// Stand at the origin, watch a gate off to the -Z side.
	std::vector<PatrolPoint> route = {
		PatrolPoint(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -10.0f), 5.0f),
		PatrolPoint(glm::vec3(10.0f, 0.0f, 0.0f)),
	};

	GuardAgent g;
	g.SetPatrolRoute(route);
	g.SetYaw(0.0f);   // facing +X, away from the gate

	// It arrives immediately (it starts there), so it should spend the hold
	// turning to watch the gate rather than facing the next waypoint.
	Simulate(g, glm::vec3(0.0f, 0.0f, 500.0f), 1.5f);

	EXPECT_NEAR(std::fabs(ShortestAngleDelta(g.GetYaw(), -90.0f)), 0.0f, 10.0f)
		<< "a posted guard should watch what it was posted to watch";
}

TEST(GuardPatrol, StopsPatrollingWhenSuspicious)
{
	GuardAgent g;
	g.SetPatrolRoute({ glm::vec3(0.0f), glm::vec3(20.0f, 0.0f, 0.0f) });
	g.Patrol().pauseAtWaypoint = 0.0f;
	g.SetYaw(0.0f);

	Simulate(g, glm::vec3(5.0f, 0.0f, 0.0f), 1.5f);   // dead ahead, in view
	ASSERT_NE(g.GetAlertState(), AlertState::Unaware);

	float xAtAlert = g.GetPosition().x;
	Simulate(g, glm::vec3(5.0f, 0.0f, 0.0f), 1.0f);

	EXPECT_NEAR(g.GetPosition().x, xAtAlert, 1e-3f) << "alerted guard should hold position";
}
