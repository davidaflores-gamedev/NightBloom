//------------------------------------------------------------------------------
// GuardAgent.cpp
//------------------------------------------------------------------------------
#include "Engine/Gameplay/GuardAgent.hpp"
#include "Engine/Math/MathUtils.hpp"   // ShortestAngleDelta

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Nightbloom
{
	// ShortestAngleDelta now lives in Engine/Math/MathUtils.hpp — the character
	// controller slews its facing the same way, and one copy beats two.

	void GuardAgent::SetPatrolRoute(const std::vector<glm::vec3>& positions)
	{
		std::vector<PatrolPoint> points;
		points.reserve(positions.size());
		for (const auto& p : positions) points.emplace_back(p);
		SetPatrolRoute(std::move(points));
	}

	void GuardAgent::SetPatrolRoute(std::vector<PatrolPoint> waypoints)
	{
		m_Waypoints = std::move(waypoints);
		m_WaypointIndex = 0;
		m_PatrolDirection = 1;
		m_PauseTimer = 0.0f;
		m_BestDistanceToWaypoint = std::numeric_limits<float>::max();
		m_NoProgressTimer = 0.0f;

		// Start standing on the first waypoint, facing the second, so a guard
		// never spends its first seconds walking in from wherever it defaulted.
		if (!m_Waypoints.empty())
		{
			m_Position = m_Waypoints[0].position;
			if (m_Waypoints.size() > 1)
			{
				glm::vec3 toNext = m_Waypoints[1].position - m_Waypoints[0].position;
				if (glm::length(toNext) > 1e-4f)
					m_Yaw = glm::degrees(std::atan2(toNext.z, toNext.x));
			}
			SnapToGround();
		}
	}

	glm::vec3 GuardAgent::GetForward() const
	{
		const float y = glm::radians(m_Yaw);
		return glm::vec3(std::cos(y), 0.0f, std::sin(y));
	}

	glm::vec3 GuardAgent::GetEyePosition() const
	{
		return m_Position + glm::vec3(0.0f, m_Vision.eyeHeight, 0.0f);
	}

	bool GuardAgent::IsBehind(const glm::vec3& worldPoint) const
	{
		glm::vec3 toPoint = worldPoint - m_Position;
		toPoint.y = 0.0f;

		const float lenSq = glm::dot(toPoint, toPoint);
		if (lenSq < 1e-8f) return true;   // same spot; see header note

		return glm::dot(GetForward(), toPoint / std::sqrt(lenSq)) < 0.0f;
	}

	glm::mat4 GuardAgent::GetVisualTransform() const
	{
		// Same convention as Pawn::GetVisualTransform — mesh front is +X, so
		// rotateY(-yaw) maps it onto the heading.
		return glm::translate(glm::mat4(1.0f), m_Position) *
			glm::rotate(glm::mat4(1.0f), glm::radians(-m_Yaw), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	//--------------------------------------------------------------------------
	// Perception
	//--------------------------------------------------------------------------

	// Returns whether the target is visible this frame. `outCloseness` comes back
	// in [0, 1] describing how *strongly* it's seen — 1 = point blank and dead
	// centre, 0 = at the very edge of range or the very edge of the cone. That
	// scalar is what makes detection speed feel fair rather than binary.
	bool GuardAgent::EvaluateSight(const PerceptionTarget& target, float& outCloseness) const
	{
		// Aim at the target's centre of mass rather than its feet: a feet-level
		// ray clips every pebble and rise between here and there, so a guard
		// would lose sight of a player standing in plain view on a slope.
		//
		// Using the TARGET's height (not the guard's) is what makes crouching
		// work: drop the height and the aim point drops with it, so waist-high
		// cover starts intercepting the ray. No stealth special-case — just
		// geometry.
		const glm::vec3 targetEye = target.position + glm::vec3(0.0f, target.height * 0.6f, 0.0f);
		return EvaluateSightAt(targetEye, target.rangeExposure, outCloseness);
	}

	bool GuardAgent::CanSeePoint(const glm::vec3& worldPoint, float pointHeight,
		float rangeExposure) const
	{
		float ignored = 0.0f;
		return EvaluateSightAt(worldPoint + glm::vec3(0.0f, pointHeight, 0.0f),
			rangeExposure, ignored);
	}

	bool GuardAgent::EvaluateSightAt(const glm::vec3& targetEye, float rangeExposure, float& outCloseness) const
	{
		outCloseness = 0.0f;

		// Concealment (shadow / stance / cover) shrinks how far away the target
		// registers. Floored at minAwarenessRange so stacked concealment can
		// never hide you from someone standing over you — see VisionConfig.
		const float effectiveRange = std::max(
			m_Vision.rangeMeters * std::clamp(rangeExposure, 0.0f, 1.0f),
			m_Vision.minAwarenessRange);

		const glm::vec3 eye = GetEyePosition();
		glm::vec3 toTarget = targetEye - eye;
		float distance = glm::length(toTarget);
		if (distance > effectiveRange) return false;
		if (distance < 1e-4f) { outCloseness = 1.0f; return true; }

		glm::vec3 dirToTarget = toTarget / distance;

		// Vertical (pitch) limits, asymmetric — see VisionConfig. This is what
		// makes elevation meaningful: steeply above a nearby guard is outside
		// their scan; shallowly above a distant one is not. Downward is
		// deliberately generous so a player on a slope below is still seen.
		const float pitchDegrees = glm::degrees(std::asin(std::clamp(dirToTarget.y, -1.0f, 1.0f)));
		if (pitchDegrees > m_Vision.lookUpDegrees) return false;
		if (pitchDegrees < -m_Vision.lookDownDegrees) return false;

		// Horizontal cone test. Pitch is handled separately above rather than as
		// one combined solid angle — a true cone narrows at the edges in a way
		// that's very hard to read while playing.
		glm::vec3 flatDir = glm::vec3(dirToTarget.x, 0.0f, dirToTarget.z);
		float flatLen = glm::length(flatDir);
		if (flatLen < 1e-4f) return false;      // directly overhead/below
		flatDir /= flatLen;

		const glm::vec3 forward = GetForward();
		float cosAngle = glm::dot(forward, flatDir);
		float cosHalfAngle = std::cos(glm::radians(m_Vision.halfAngleDegrees));
		if (cosAngle < cosHalfAngle) return false;   // outside the cone

		// Occlusion last: it's the most expensive test, so range and angle
		// reject the common cases first.
		if (m_LosFn && !m_LosFn(eye, targetEye)) return false;

		// Falloff. Distance term is linear against the EFFECTIVE range, so a
		// shadowed target also detects more slowly at any given distance, not
		// just at a shorter cutoff.
		float distanceFactor = 1.0f - (distance / effectiveRange);
		float angleFactor = (cosHalfAngle < 1.0f)
			? (cosAngle - cosHalfAngle) / (1.0f - cosHalfAngle)
			: 1.0f;

		outCloseness = std::clamp(distanceFactor * angleFactor, 0.0f, 1.0f);
		return true;
	}

	// How strongly this guard hears the target, [0,1]. Deliberately ignores the
	// vision cone and line of sight: a guard hears footsteps behind them and
	// through a wall. Louder noise carries further, so sprinting is audible
	// across the camp while a crouch-walk is only heard from a few metres.
	float GuardAgent::EvaluateHearing(const PerceptionTarget& target) const
	{
		const float noise = std::clamp(target.noise, 0.0f, 1.0f);
		if (noise <= 0.0f) return 0.0f;

		const float audibleRange = m_Vision.hearingRange * noise;
		if (audibleRange <= 1e-3f) return 0.0f;

		glm::vec3 toTarget = target.position - m_Position;
		toTarget.y = 0.0f;                       // hearing is flat; height doesn't muffle
		const float distance = glm::length(toTarget);
		if (distance >= audibleRange) return 0.0f;

		// Loudness x proximity falloff.
		return noise * (1.0f - distance / audibleRange);
	}

	//--------------------------------------------------------------------------
	// Patrol
	//--------------------------------------------------------------------------

	void GuardAgent::TurnToward(const glm::vec3& worldTarget, float dt)
	{
		glm::vec3 delta = worldTarget - m_Position;
		delta.y = 0.0f;
		if (glm::length(delta) < 1e-4f) return;

		float desiredYaw = glm::degrees(std::atan2(delta.z, delta.x));
		float diff = ShortestAngleDelta(m_Yaw, desiredYaw);
		float maxStep = m_Patrol.turnSpeedDegrees * dt;

		m_Yaw += std::clamp(diff, -maxStep, maxStep);
	}

	void GuardAgent::SnapToGround()
	{
		if (m_GroundFn)
			m_Position.y = m_GroundFn(m_Position.x, m_Position.z);
	}

	// Push out of anything solid, then re-settle on the ground. Called after
	// every move so a guard cannot end a frame inside a wall — routes that
	// clip geometry now slide along it instead of passing through, which keeps
	// patrol timings honest even if a waypoint was placed carelessly.
	void GuardAgent::ResolveCollision()
	{
		if (!m_CollideFn) return;
		m_Position = m_CollideFn(m_Position, m_Patrol.radius, m_Patrol.height);
		SnapToGround();
	}

	void GuardAgent::InvestigatePoint(const glm::vec3& worldPoint)
	{
		// A guard who already has you doesn't get pulled away — otherwise a
		// thrown stone would be a get-out-of-jail card.
		if (m_State == AlertState::Alerted) return;

		m_Investigating = true;
		m_InvestigatePoint = worldPoint;
		m_InvestigateArrived = false;
		m_InvestigateTimer = m_Patrol.investigateLookTime;
	}

	// Walk to the disturbance, then sweep the area before giving up. The sweep
	// matters: a guard that arrives, stares straight ahead and leaves is trivial
	// to slip behind, which would make distractions strictly free.
	void GuardAgent::UpdateInvestigation(float dt)
	{
		glm::vec3 toPoint = m_InvestigatePoint - m_Position;
		toPoint.y = 0.0f;
		const float distance = glm::length(toPoint);

		if (!m_InvestigateArrived && distance > m_Patrol.investigateArrive)
		{
			TurnToward(m_InvestigatePoint, dt);
			m_Position += GetForward() * (m_Patrol.moveSpeed * m_Patrol.investigateSpeedMul) * dt;
			SnapToGround();
			ResolveCollision();
			return;
		}

		m_InvestigateArrived = true;
		m_InvestigateTimer -= dt;

		if (m_InvestigateTimer <= 0.0f)
		{
			// Nothing here. Resume the route from whichever waypoint is closest,
			// rather than trekking back to the one it abandoned — that read as
			// the guard ignoring where it actually stood.
			m_Investigating = false;
			m_PauseTimer = 0.0f;

			if (!m_Waypoints.empty())
			{
				size_t nearest = 0;
				float  bestSq = std::numeric_limits<float>::max();
				for (size_t i = 0; i < m_Waypoints.size(); ++i)
				{
					glm::vec3 d = m_Waypoints[i].position - m_Position;
					d.y = 0.0f;
					float sq = glm::dot(d, d);
					if (sq < bestSq) { bestSq = sq; nearest = i; }
				}
				m_WaypointIndex = nearest;
				m_BestDistanceToWaypoint = std::numeric_limits<float>::max();
				m_NoProgressTimer = 0.0f;
			}
			return;
		}

		// Sweep: swing the facing across the area while searching.
		const float phase = 1.0f - (m_InvestigateTimer / std::max(m_Patrol.investigateLookTime, 1e-3f));
		const float sweep = std::sin(phase * 6.2831853f) * (m_Patrol.investigateSweepDegrees * 0.5f);

		glm::vec3 baseDir = m_InvestigatePoint - m_Position;
		baseDir.y = 0.0f;
		const float baseYaw = (glm::dot(baseDir, baseDir) > 1e-6f)
			? glm::degrees(std::atan2(baseDir.z, baseDir.x))
			: m_Yaw;

		const float desired = baseYaw + sweep;
		const float diff = ShortestAngleDelta(m_Yaw, desired);
		const float maxStep = m_Patrol.turnSpeedDegrees * dt;
		m_Yaw += std::clamp(diff, -maxStep, maxStep);
	}

	void GuardAgent::UpdatePatrol(float dt)
	{
		if (m_Waypoints.size() < 2) return;   // nothing to walk between

		const PatrolPoint& point = m_Waypoints[m_WaypointIndex];

		// Pausing at a waypoint: stand and look around rather than pivoting
		// instantly to the next leg. Reads as a person, and gives the player a
		// predictable window. If the post has an intent — a gate, a doorway —
		// face THAT while standing there, which is what makes a stop read as a
		// post rather than an arbitrary coordinate.
		if (m_PauseTimer > 0.0f)
		{
			m_PauseTimer -= dt;
			// Use the look target captured on ARRIVAL, not the current
			// waypoint's: the index has already advanced to the next leg by
			// now, so reading it here would have the guard stand at one post
			// staring at where it's going next.
			if (m_PauseHasLookTarget)
				TurnToward(m_PauseLookTarget, dt);
			return;
		}

		const glm::vec3& target = point.position;

		glm::vec3 toTarget = target - m_Position;
		toTarget.y = 0.0f;                     // terrain height is handled by the snap
		float distance = glm::length(toTarget);

		// Progress tracking. If the guard stops closing on its waypoint for a
		// while, the waypoint is effectively unreachable — take the next one
		// rather than grinding against whatever is in the way forever.
		if (distance < m_BestDistanceToWaypoint - 0.05f)
		{
			m_BestDistanceToWaypoint = distance;
			m_NoProgressTimer = 0.0f;
		}
		else
		{
			m_NoProgressTimer += dt;
		}

		const bool arrived = (distance <= m_Patrol.waypointRadius);
		const bool givingUp = (m_NoProgressTimer >= m_Patrol.stuckTimeout);

		if (arrived || givingUp)
		{
			// Only pause if it actually got there; a skipped waypoint shouldn't
			// reward the guard with a rest.
			m_PauseTimer = arrived
				? (point.pauseOverride >= 0.0f ? point.pauseOverride : m_Patrol.pauseAtWaypoint)
				: 0.0f;

			// Capture this post's intent before the index moves on.
			m_PauseHasLookTarget = arrived && point.hasLookTarget;
			m_PauseLookTarget = point.lookTarget;

			m_BestDistanceToWaypoint = std::numeric_limits<float>::max();
			m_NoProgressTimer = 0.0f;

			if (m_Patrol.pingPong)
			{
				// Reverse at the ends instead of wrapping, so the guard retraces
				// its route rather than teleporting its heading at the loop point.
				if (m_WaypointIndex == m_Waypoints.size() - 1) m_PatrolDirection = -1;
				else if (m_WaypointIndex == 0)                 m_PatrolDirection = 1;

				int next = static_cast<int>(m_WaypointIndex) + m_PatrolDirection;
				m_WaypointIndex = static_cast<size_t>(std::clamp(next, 0,
					static_cast<int>(m_Waypoints.size()) - 1));
			}
			else
			{
				m_WaypointIndex = (m_WaypointIndex + 1) % m_Waypoints.size();
			}
			return;
		}

		// How far off the destination we're currently pointed, BEFORE turning.
		const float desiredYaw = glm::degrees(std::atan2(toTarget.z, toTarget.x));
		const float misalignment = std::fabs(ShortestAngleDelta(m_Yaw, desiredYaw));

		TurnToward(target, dt);

		// Walk along the heading rather than straight at the waypoint, so the
		// turn is visible instead of the guard strafing sideways into place —
		// but only once roughly aligned. Setting off while badly misaligned
		// makes the guard arc past the waypoint, miss its arrival radius, and
		// orbit it indefinitely. Turning in place first guarantees it converges.
		if (misalignment <= m_Patrol.moveAlignmentDegrees)
		{
			m_Position += GetForward() * m_Patrol.moveSpeed * dt;
			SnapToGround();
			ResolveCollision();
		}
	}

	//--------------------------------------------------------------------------
	// Update
	//--------------------------------------------------------------------------

	void GuardAgent::Update(float dt, const PerceptionTarget& target)
	{
		if (dt <= 0.0f) return;

		float closeness = 0.0f;
		m_HasSight = EvaluateSight(target, closeness);

		if (m_HasSight)
		{
			m_LastKnownTargetPos = target.position;

			// Detection rate = (proximity + motion), scaled by the target's
			// visibility profile and the guard's alertness.
			//
			//  proximity — quadratic in closeness, so point-blank-dead-centre
			//    confirms FAST and the far edge of the cone is slow. (The old
			//    linear 0.25+0.75c made close range feel sluggish.)
			//  motion — weighted toward LOW closeness, so a running figure is
			//    caught at range where a still one blends in. This is what makes
			//    "sprint across the open at distance" dangerous while "creep at
			//    distance" stays safe.
			const float proximityTerm = 0.15f + 1.5f * closeness * closeness;
			const float motionTerm     = std::clamp(target.motion, 0.0f, 1.0f)
				* (0.35f + 0.65f * (1.0f - closeness));

			float visibility = std::clamp(target.visibility, 0.05f, 1.0f);
			float alertness  = m_Investigating ? m_Vision.investigateAlertness : 1.0f;
			float rate = alertness * visibility * (proximityTerm + motionTerm)
				/ std::max(m_Vision.timeToDetect, 1e-3f);
			m_Detection = std::min(1.0f, m_Detection + rate * dt);
		}
		else if (const float heard = EvaluateHearing(target); heard > 0.0f)
		{
			// Heard but not seen. Sound points them at you and can make them
			// suspicious enough to come looking, but is capped below full
			// confirmation — noise DRAWS, sight CONFIRMS.
			m_LastKnownTargetPos = target.position;

			const float ceiling = std::clamp(m_Vision.hearingMaxDetection, 0.0f, 1.0f);
			if (m_Detection < ceiling)
				m_Detection = std::min(ceiling, m_Detection + heard * m_Vision.hearingRate * dt);

			// Once it registers as more than a twitch, go and look. This is what
			// makes running past a guard behind cover actually pull them.
			if (!m_Investigating && m_Detection > m_Vision.suspicionThreshold)
				InvestigatePoint(target.position);
		}
		else
		{
			float rate = 1.0f / std::max(m_Vision.timeToForget, 1e-3f);
			m_Detection = std::max(0.0f, m_Detection - rate * dt);
		}

		// Derive state from the meter, with investigation filling the gap
		// between "nothing" and "saw something": the guard has a reason to be
		// somewhere without having seen you at all.
		if (m_Detection >= 1.0f)                           m_State = AlertState::Alerted;
		else if (m_Detection > m_Vision.suspicionThreshold) m_State = AlertState::Suspicious;
		else if (m_Investigating)                          m_State = AlertState::Investigating;
		else                                               m_State = AlertState::Unaware;

		switch (m_State)
		{
		case AlertState::Alerted:
			// Confirmed: hold on the target. Chasing is a combat-arc problem;
			// stopping and staring is enough to read as "you've been made".
			TurnToward(m_LastKnownTargetPos, dt);
			m_Investigating = false;   // it has a real target now
			break;

		case AlertState::Suspicious:
			// Something caught its eye — turn to look, but don't abandon the
			// post. This is the window the player gets to break line of sight.
			TurnToward(m_LastKnownTargetPos, dt);
			break;

		case AlertState::Investigating:
			UpdateInvestigation(dt);
			break;

		case AlertState::Unaware:
		default:
			UpdatePatrol(dt);
			break;
		}
	}

	//--------------------------------------------------------------------------

	glm::vec3 GuardAgent::GetDebugTint() const
	{
		// Green -> amber -> red across the meter. Two-segment lerp rather than a
		// straight green->red, which would pass through a muddy brown midpoint
		// exactly where the player most needs to read the state.
		const glm::vec3 calm(0.25f, 0.85f, 0.35f);
		const glm::vec3 warn(1.00f, 0.75f, 0.10f);
		const glm::vec3 alert(1.00f, 0.15f, 0.10f);

		// An investigating guard reads as at-least-mildly-alarmed even with an
		// empty meter, so a lured guard is visibly different from a bored one —
		// otherwise a distraction produces no feedback at all.
		float d = std::clamp(m_Detection, 0.0f, 1.0f);
		if (m_Investigating) d = std::max(d, 0.35f);
		return (d < 0.5f) ? glm::mix(calm, warn, d * 2.0f)
			: glm::mix(warn, alert, (d - 0.5f) * 2.0f);
	}

} // namespace Nightbloom
