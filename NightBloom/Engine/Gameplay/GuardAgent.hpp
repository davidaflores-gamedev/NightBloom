//------------------------------------------------------------------------------
// GuardAgent.hpp
//
// A patrolling guard with a vision cone — ROADMAP 3.3, the first real stealth
// mechanic and the thing that answers "is this fun?".
//
// Not a Pawn. Pawn is shaped around PawnInput (a frame of *player* intent);
// a guard generates its own intent, so inheriting that would mean faking an
// input struct every frame. It shares Pawn's conventions instead — same yaw
// meaning (heading = (cos yaw, 0, sin yaw)), same injected GroundSampler — so
// the two read the same way and a guard can be swapped onto terrain or a flat
// floor without change.
//
// Dependencies are injected as std::function, deliberately: this class has no
// idea what a Scene, a TerrainSystem, or a Renderer is. That keeps it testable
// and means a future navmesh replaces the patrol source without touching
// perception.
//
// DESIGN NOTE — detection is a METER, not a boolean.
// Instant spotting reads as unfair and kills tension: you're either invisible
// or caught, with no moment in between. A meter that fills while you're seen
// and drains while you're not creates the actual stealth beat — the "break
// line of sight NOW" scramble — and gives the player readable feedback about
// how exposed they are. Fill rate scales with proximity and with how centred
// you are in the cone, so sprinting across a guard's nose is punished far
// harder than edging through their peripheral vision at distance.
//------------------------------------------------------------------------------
#pragma once

#include <glm/glm.hpp>
#include <functional>
#include <limits>
#include <vector>

namespace Nightbloom
{
	//--------------------------------------------------------------------------
	// What the guard currently believes. Drives both behaviour and the debug
	// tint, so the player can read state off the guard's body at a glance.
	//--------------------------------------------------------------------------
	enum class AlertState
	{
		Unaware = 0,   // patrolling, meter empty
		Investigating, // heading to a disturbance — a noise, or a body
		Suspicious,    // meter filling or draining — "did I see something?"
		Alerted        // meter full — target confirmed
	};

	//--------------------------------------------------------------------------
	// Perception tuning. Separated from the agent so encounters can vary guards
	// (a drowsy patrol vs. an alert captain) without subclassing.
	//--------------------------------------------------------------------------
	struct VisionConfig
	{
		float rangeMeters = 28.0f;   // hard cut-off for sight (standing, unconcealed)
		float halfAngleDegrees = 45.0f;   // half the cone's width (so 90 deg total)
		float eyeHeight = 1.7f;    // ray origin above the guard's feet

		// VERTICAL limits on the cone, and they are asymmetric on purpose.
		//
		// People on patrol scan the ground and the middle distance; they don't
		// walk around staring at the rooftops. Without an upward limit, climbing
		// onto a wall is suicide — you're elevated, in the open, and inside
		// everyone's cone at once, which kills the whole reason to climb.
		//
		// With it, height behaves the way it should: standing above a NEARBY
		// guard is safe (steep angle, outside their scan), but a DISTANT guard
		// sees you fine (shallow angle). So rooftops reward moving and punish
		// loitering in the open — a vantage point, not a hiding place.
		float lookUpDegrees = 22.0f;
		float lookDownDegrees = 70.0f;   // generous: they must see the ground ahead

		// Range inside which concealment stops mattering. Stance, shadow and
		// grass all multiply into the effective sight range, and stacked they
		// collapsed it to centimetres — you could take guards down while they
		// stood on top of you. Concealment should hide you from a DISTANCE, never
		// from someone standing over you, so the effective range never drops
		// below this. Cone and line-of-sight still apply.
		float minAwarenessRange = 3.0f;

		// Seconds of continuous, dead-centre, close-range sight to go from
		// empty to fully alerted. Distance and cone-edge falloff make real
		// detections slower than this.
		float timeToDetect = 1.2f;

		// Seconds to drain a full meter back to zero once sight is broken.
		// Deliberately slower than timeToDetect: a guard who instantly forgets
		// you removes all consequence from being spotted.
		float timeToForget = 4.0f;

		// Meter level at which the guard starts reacting (turning to look).
		float suspicionThreshold = 0.15f;

		// --- Hearing ----------------------------------------------------------
		// A separate perception channel: no cone, no line-of-sight. Sound is how
		// a guard notices what they can't see, which is what makes stance and
		// speed matter even in perfect cover.
		//
		// Hearing alone can never fully confirm you — it's capped below 1 — so
		// noise DRAWS a guard and sight CONFIRMS. That split is what stops
		// "heard a footstep" from meaning "instantly caught".
		float hearingRange = 20.0f;  // at full loudness; scales with noise
		float hearingRate = 0.7f;   // meter/sec at point-blank full noise
		float hearingMaxDetection = 0.65f;  // ceiling for sound-only detection

		// While investigating, the guard is switched on: it detects faster than
		// a bored guard on its rounds. This is what stops a distraction being
		// pure profit — pulling someone toward you makes them dangerous once
		// they arrive.
		float investigateAlertness = 1.6f;   // multiplier on detection rate
	};

	//--------------------------------------------------------------------------
	// A stop on a patrol route.
	//
	// More than a position, because "walk to a coordinate and then stare at the
	// next coordinate" is what makes patrols feel aimless: a sentry posted at a
	// gate should WATCH THE GATE while it stands there, not face the direction
	// it's about to leave in. A destination with an intent reads as a post.
	//--------------------------------------------------------------------------
	struct PatrolPoint
	{
		glm::vec3 position{ 0.0f };

		// Where to look while paused here. Unset = face along the route.
		bool      hasLookTarget = false;
		glm::vec3 lookTarget{ 0.0f };

		// Seconds to hold this post; < 0 uses PatrolConfig::pauseAtWaypoint.
		float     pauseOverride = -1.0f;

		PatrolPoint() = default;
		// Explicit: an implicit vec3 conversion makes a braced route list
		// ambiguous between the two SetPatrolRoute overloads.
		explicit PatrolPoint(const glm::vec3& p) : position(p) {}
		PatrolPoint(const glm::vec3& p, const glm::vec3& look, float pause = -1.0f)
			: position(p), hasLookTarget(true), lookTarget(look), pauseOverride(pause) {}
	};

	//--------------------------------------------------------------------------
	// Patrol tuning.
	//--------------------------------------------------------------------------
	struct PatrolConfig
	{
		float moveSpeed = 2.2f;   // m/s while patrolling
		float turnSpeedDegrees = 220.0f; // how fast facing slews toward a target
		float waypointRadius = 0.6f;   // "close enough" to advance
		float pauseAtWaypoint = 1.5f;   // seconds spent looking around on arrival
		bool  pingPong = false;  // false = loop the route, true = reverse at the ends

		// Investigation. Guards hurry toward a disturbance, then sweep the area
		// before giving up — a guard who arrives, glances, and leaves is barely
		// worth luring.
		float investigateSpeedMul = 1.6f;   // faster than a patrol stroll
		float investigateArrive = 1.5f;   // "close enough" to the disturbance
		float investigateLookTime = 3.0f;   // seconds spent searching on arrival
		float investigateSweepDegrees = 100.0f;  // how wide the search sweep is

		// Don't walk while badly misaligned with the destination. Walking along
		// the facing looks natural, but if the guard sets off before turning it
		// arcs past the waypoint, misses the arrival radius, and orbits it
		// forever — the "never reaches its destination" bug. Turning in place
		// first guarantees convergence.
		float moveAlignmentDegrees = 55.0f;

		// If no progress toward the current waypoint for this long, give up and
		// take the next one. A backstop for a waypoint that has become
		// unreachable (blocked by geometry, or shoved off by collision) — a
		// stuck guard is worse than a slightly wrong route, because it silently
		// removes a patrol the player is timing against.
		float stuckTimeout = 4.0f;

		// Body size, for world collision. A guard that clips through the wall
		// it's meant to be patrolling around destroys the whole illusion — and
		// worse, makes routes unlearnable, because the thing you're timing
		// doesn't obey the geometry you're hiding behind.
		float radius = 0.4f;
		float height = 1.8f;
	};

	//--------------------------------------------------------------------------
	// What the guard is looking for.
	//
	// Height and visibility are separate because crouching does two different
	// things and only one of them is a fudge factor:
	//  - `height` lowers where the sight ray is AIMED, so low cover genuinely
	//    starts blocking. That's real geometry, not a stealth stat, and it's the
	//    part that makes crouching behind a wall feel earned.
	//  - `visibility` scales how fast detection accrues once you ARE seen,
	//    standing in for a smaller, less motion-legible silhouette. Without it,
	//    crouching in the open would be pure downside.
	//--------------------------------------------------------------------------
	struct PerceptionTarget
	{
		glm::vec3 position{ 0.0f };  // feet
		float     height = 1.7f;   // eye height of the target, metres

		// Two independent exposure levers, both in the player's control:
		//  - visibility   scales the detection RATE (how fast the meter fills).
		//  - rangeExposure scales the effective sight RANGE (whether, and how
		//    strongly, you register at a distance at all).
		// Crouch lowers rate; shadow lowers both — which is what lets a guard
		// walk right past you in the dark instead of just spotting you slowly.
		float     visibility = 1.0f;
		float     rangeExposure = 1.0f;

		// How conspicuous the target's MOTION is, [0,1] (still = 0, sprint = 1).
		// Motion is weighted to matter MORE at range — a moving figure across
		// the camp catches the eye where a still one blends in — so running is
		// caught at distances where sneaking is safe.
		float     motion = 0.0f;

		// How much NOISE the target is making, [0,1]. Heard without line of
		// sight and without a facing cone — sound goes around corners and comes
		// from behind, which is exactly what makes it a separate channel from
		// sight rather than a modifier on it.
		float     noise = 0.0f;
	};

	//--------------------------------------------------------------------------
	// GuardAgent
	//--------------------------------------------------------------------------
	class GuardAgent
	{
	public:
		// Height query for snapping to the ground; matches the pawns' convention.
		using GroundSampler = std::function<float(float x, float z)>;

		// Occlusion test between two world points. Wired to Scene::HasLineOfSight
		// by the app; nullptr means "nothing blocks sight" (open-field testing).
		using LineOfSightFn = std::function<bool(const glm::vec3& from, const glm::vec3& to)>;

		// Same contract as CharacterPawn's: push a desired feet position out of
		// world geometry. Guards use it so they physically cannot walk through
		// the walls they patrol.
		using CollisionResolver =
			std::function<glm::vec3(const glm::vec3& feetPosition, float radius, float height)>;

		GuardAgent() = default;

		// --- Setup ------------------------------------------------------------
		void SetPatrolRoute(std::vector<PatrolPoint> waypoints);
		void SetPatrolRoute(const std::vector<glm::vec3>& positions);   // plain-position convenience
		void SetGroundSampler(GroundSampler fn) { m_GroundFn = std::move(fn); }
		void SetLineOfSightFn(LineOfSightFn fn) { m_LosFn = std::move(fn); }
		void SetCollisionResolver(CollisionResolver fn) { m_CollideFn = std::move(fn); }

		VisionConfig& Vision() { return m_Vision; }
		PatrolConfig& Patrol() { return m_Patrol; }
		const VisionConfig& Vision() const { return m_Vision; }
		const PatrolConfig& Patrol() const { return m_Patrol; }

		// --- Simulation -------------------------------------------------------
		void Update(float dt, const PerceptionTarget& target);

		// Convenience overload: a standing, fully-visible target at `position`
		// (feet). Keeps simple callers and tests from constructing a struct.
		void Update(float dt, const glm::vec3& targetPosition)
		{
			PerceptionTarget t;
			t.position = targetPosition;
			Update(dt, t);
		}

		// --- Stimulus ---------------------------------------------------------
		// Send the guard to look at something: a thrown stone landing, a body,
		// a shout. This is the hook that turns stealth from waiting into
		// planning — the player can now MAKE an opening instead of watching for
		// one.
		//
		// Ignored while Alerted: a guard who has you doesn't get distracted,
		// otherwise a stone would be an escape button.
		void InvestigatePoint(const glm::vec3& worldPoint);

		// Can this guard see an arbitrary point (a body, a dropped item)?
		// Same range/cone/occlusion rules as target perception, exposed so the
		// game can ask about things that aren't the player.
		//
		// `rangeExposure` lets the caller apply the SAME concealment the player
		// gets — a body lying in tall grass has to be hidden by it too, or the
		// world contradicts itself (grass hides you but not your victim).
		bool CanSeePoint(const glm::vec3& worldPoint, float pointHeight = 0.5f,
			float rangeExposure = 1.0f) const;

		// Clear all perception state (meter, sight, investigation) without
		// touching tuning or the patrol route. For re-running a measurement.
		void ResetPerception()
		{
			m_Detection = 0.0f;
			m_HasSight = false;
			m_State = AlertState::Unaware;
			m_Investigating = false;
			m_InvestigateArrived = false;
			m_InvestigateTimer = 0.0f;
		}

		bool IsInvestigating() const { return m_Investigating; }
		const glm::vec3& GetInvestigatePoint() const { return m_InvestigatePoint; }

		// --- State ------------------------------------------------------------
		const glm::vec3& GetPosition() const { return m_Position; }
		void  SetPosition(const glm::vec3& p) { m_Position = p; }
		float GetYaw() const { return m_Yaw; }
		void  SetYaw(float yawDegrees) { m_Yaw = yawDegrees; }

		glm::vec3  GetForward() const;
		glm::vec3  GetEyePosition() const;
		glm::mat4  GetVisualTransform() const;

		// Is `worldPoint` in the guard's rear arc? The predicate a stealth
		// takedown is gated on, so it lives here with the facing convention it
		// depends on rather than being re-derived by every caller.
		//
		// Purely horizontal: standing on a crate above a guard shouldn't stop
		// you being behind them. A point at the guard's exact position counts
		// as behind — degenerate, but "not behind" would be the stranger answer.
		bool IsBehind(const glm::vec3& worldPoint) const;

		AlertState GetAlertState() const { return m_State; }
		float      GetDetection() const { return m_Detection; }   // [0, 1]
		bool       CanSeeTarget() const { return m_HasSight; }    // this frame

		// Where the guard last actually saw the target. Only meaningful once
		// the meter has risen above zero; the investigate behaviour that will
		// consume this properly is 3.4 work.
		const glm::vec3& GetLastKnownTargetPos() const { return m_LastKnownTargetPos; }

		// Debug tint for the placeholder body: green -> amber -> red as the
		// meter fills. Lets you read detection off the guard without any UI.
		glm::vec3 GetDebugTint() const;

	private:
		bool  EvaluateSight(const PerceptionTarget& target, float& outCloseness) const;
		bool  EvaluateSightAt(const glm::vec3& targetEye, float rangeExposure, float& outCloseness) const;
		float EvaluateHearing(const PerceptionTarget& target) const;
		void  UpdatePatrol(float dt);
		void  UpdateInvestigation(float dt);
		void  TurnToward(const glm::vec3& worldTarget, float dt);
		void  SnapToGround();
		void  ResolveCollision();

		// --- Config ---
		VisionConfig m_Vision;
		PatrolConfig m_Patrol;

		// --- Injected deps ---
		GroundSampler     m_GroundFn;
		LineOfSightFn     m_LosFn;
		CollisionResolver m_CollideFn;

		// --- Transform ---
		glm::vec3 m_Position = glm::vec3(0.0f);
		float     m_Yaw = 0.0f;

		// --- Patrol state ---
		std::vector<PatrolPoint> m_Waypoints;
		size_t m_WaypointIndex = 0;
		int    m_PatrolDirection = 1;      // +1 / -1, only used when pingPong
		float  m_PauseTimer = 0.0f;

		// Stuck detection: best distance achieved toward the current waypoint,
		// and how long since it last improved.
		float  m_BestDistanceToWaypoint = std::numeric_limits<float>::max();
		float  m_NoProgressTimer = 0.0f;

		// The arrived-at post's intent, captured before the index advances.
		bool      m_PauseHasLookTarget = false;
		glm::vec3 m_PauseLookTarget{ 0.0f };

		// --- Perception state ---
		AlertState m_State = AlertState::Unaware;
		float      m_Detection = 0.0f;   // [0, 1]
		bool       m_HasSight = false;
		glm::vec3  m_LastKnownTargetPos = glm::vec3(0.0f);

		// --- Investigation state ---
		bool      m_Investigating = false;
		glm::vec3 m_InvestigatePoint = glm::vec3(0.0f);
		float     m_InvestigateTimer = 0.0f;   // counts down the search on arrival
		bool      m_InvestigateArrived = false;
	};

} // namespace Nightbloom
