//------------------------------------------------------------------------------
// CharacterPawn.cpp
//------------------------------------------------------------------------------
#include "Engine/Gameplay/CharacterPawn.hpp"

#include <algorithm>
#include <cmath>

namespace Nightbloom
{
	float CharacterPawn::GetHorizontalSpeed() const
	{
		return glm::length(glm::vec3(m_Velocity.x, 0.0f, m_Velocity.z));
	}

	namespace
	{
		// Piecewise lerp across the three stance anchors by m_StanceValue in
		// [0,2]: stand at 0, crouch at 1, prone at 2.
		float StanceLerp(float v, float stand, float crouch, float prone)
		{
			v = std::clamp(v, 0.0f, 2.0f);
			return (v < 1.0f) ? glm::mix(stand, crouch, v)
			                  : glm::mix(crouch, prone, v - 1.0f);
		}
	}

	float CharacterPawn::CurrentEyeHeight() const
	{
		return StanceLerp(m_StanceValue,
			m_Config.eyeHeight, m_Config.crouchEyeHeight, m_Config.proneEyeHeight);
	}

	float CharacterPawn::GetHeightFraction() const
	{
		return StanceLerp(m_StanceValue,
			1.0f, m_Config.crouchHeightMul, m_Config.proneHeightMul);
	}

	void CharacterPawn::Update(const PawnInput& input, float dt)
	{
		if (dt <= 0.0f) return;

		// --- Mantle in progress ------------------------------------------------
		// While climbing the character is committed: no steering, no gravity,
		// no collision. That commitment is the cost that stops a mantle being a
		// free dodge, and it keeps the motion readable instead of fighting the
		// normal movement code for a few frames.
		if (m_Climbing)
		{
			m_ClimbProgress += dt / std::max(m_Config.climbDuration, 1e-3f);

			if (m_ClimbProgress >= 1.0f)
			{
				m_Position = m_ClimbEnd;
				m_Climbing = false;
				m_ClimbProgress = 0.0f;
				m_VelocityY = 0.0f;
				m_Velocity = glm::vec3(0.0f);   // arrive settled, not sprinting off the edge
				m_Grounded = true;
				m_TimeSinceGrounded = 0.0f;
				return;
			}

			// Up first, then across. A straight line reads as floating through
			// the wall; going vertical before horizontal looks like a pull-up
			// and keeps the body outside the geometry the whole way.
			const float t = m_ClimbProgress;
			const float vertical = std::min(1.0f, t * 1.6f);
			const float horizontal = std::max(0.0f, (t - 0.35f) / 0.65f);

			m_Position.x = glm::mix(m_ClimbStart.x, m_ClimbEnd.x, horizontal);
			m_Position.z = glm::mix(m_ClimbStart.z, m_ClimbEnd.z, horizontal);
			m_Position.y = glm::mix(m_ClimbStart.y, m_ClimbEnd.y, vertical);
			return;
		}

		// --- Stance -----------------------------------------------------------
		// Crouch can't be entered mid-air: it would let you shrink past a ledge
		// lip in flight, and there's no animation that sells it.
		//
		// This asks "am I standing on the ground RIGHT NOW" rather than reading
		// m_Grounded, which is only resolved at the END of this function — using
		// it here would gate on last frame's answer, and a pawn spawned standing
		// on the floor (m_Grounded still false) could not crouch at all until it
		// had been updated once.
		float stanceGroundY = m_Ground ? m_Ground(m_Position.x, m_Position.z) : 0.0f;
		if (m_Support)
			stanceGroundY = std::max(stanceGroundY,
				m_Support(m_Position, m_Config.radius, m_Config.stepHeight));
		const bool onGroundNow = (m_Position.y <= stanceGroundY + 1e-3f);

		// Prone overrides crouch (lower wins), and neither can be entered mid-air
		// — dropping stance in flight would let you shrink past a ledge lip.
		if (!onGroundNow)               m_StanceTarget = Stance::Standing;
		else if (input.prone)           m_StanceTarget = Stance::Prone;
		else if (input.crouch)          m_StanceTarget = Stance::Crouching;
		else                            m_StanceTarget = Stance::Standing;

		const float stanceTargetValue = static_cast<float>(static_cast<int>(m_StanceTarget));
		const float stanceStep = m_Config.stanceBlendSpeed * dt;
		m_StanceValue += std::clamp(stanceTargetValue - m_StanceValue, -stanceStep, stanceStep);
		m_StanceValue = std::clamp(m_StanceValue, 0.0f, 2.0f);

		// --- Wish direction ---------------------------------------------------
		// Horizontal wish-direction in the camera-yaw plane (ignore pitch).
		glm::vec3 fwd   = input.cameraForward;  fwd.y   = 0.0f;
		glm::vec3 right = input.cameraRight;    right.y = 0.0f;
		if (glm::dot(fwd, fwd) > 1e-6f)     fwd   = glm::normalize(fwd);
		if (glm::dot(right, right) > 1e-6f) right = glm::normalize(right);

		glm::vec3 wish = fwd * input.move.y + right * input.move.x;
		const bool hasInput = glm::dot(wish, wish) > 1e-6f;
		if (hasInput)
			wish = glm::normalize(wish);

		// Speed follows the (smoothed) stance, so a stance change eases the pace
		// instead of snapping it. Sprint only applies when standing — you can't
		// sprint-crawl, and crouch/prone win over sprint so there's no
		// fast-but-quiet exploit.
		float speed = m_Config.walkSpeed
			* StanceLerp(m_StanceValue, 1.0f, m_Config.crouchSpeedMul, m_Config.proneSpeedMul);
		if (m_StanceTarget == Stance::Standing && input.sprint)
			speed *= m_Config.sprintMul;

		// --- Horizontal velocity ----------------------------------------------
		// Accelerate toward the wish velocity instead of assigning it. This is
		// the whole difference between "moving" and "teleporting".
		glm::vec3 targetVelocity = hasInput ? wish * speed : glm::vec3(0.0f);

		float rate = hasInput ? m_Config.acceleration : m_Config.deceleration;
		if (!m_Grounded)
			rate *= m_Config.airControl;   // committed to your jump arc

		glm::vec3 delta = targetVelocity - m_Velocity;
		float deltaLen = glm::length(delta);
		float maxStep = rate * dt;
		// Clamp the step so we approach the target without overshooting into an
		// oscillation at low frame rates.
		m_Velocity += (deltaLen > maxStep && deltaLen > 1e-6f)
			? (delta / deltaLen) * maxStep
			: delta;

		const glm::vec3 beforeMove = m_Position;
		m_Position.x += m_Velocity.x * dt;
		m_Position.z += m_Velocity.z * dt;

		// --- World collision --------------------------------------------------
		if (m_Collide)
		{
			const float bodyHeight = m_Config.height * GetHeightFraction();
			m_Position = m_Collide(m_Position, m_Config.radius, bodyHeight, m_Config.stepHeight);

			// Rebuild horizontal velocity from the distance ACTUALLY travelled.
			// Without this, walking into a wall keeps accelerating velocity into
			// it; the pawn would then rocket sideways the moment the wall ends,
			// and stopping would take a full deceleration ramp against a surface
			// it never moved along. Deriving velocity from real displacement
			// also gives wall sliding for free — the tangential component
			// survives, the normal component is cancelled by the push-out.
			glm::vec3 actual = m_Position - beforeMove;
			glm::vec3 derived(actual.x / dt, 0.0f, actual.z / dt);

			// CLAMP: collision may only ever remove energy, never add it.
			// Depenetration (being ejected from inside a box) is a large
			// positional correction in a single frame; dividing that by dt
			// yields an enormous velocity and launches the character across the
			// map. Capping at the pre-collision speed keeps sliding intact while
			// making that impossible.
			const float previousSpeed = glm::length(glm::vec3(m_Velocity.x, 0.0f, m_Velocity.z));
			const float derivedSpeed = glm::length(derived);
			if (derivedSpeed > previousSpeed && derivedSpeed > 1e-6f)
				derived *= (previousSpeed / derivedSpeed);

			m_Velocity.x = derived.x;
			m_Velocity.z = derived.z;
		}

		// --- Facing -----------------------------------------------------------
		// Slew toward the direction of TRAVEL rather than of input, so the body
		// keeps facing its momentum through a direction change instead of
		// flipping the instant a key goes down. Below a threshold speed we hold
		// the current facing — otherwise the yaw would jitter from the noise in
		// a near-zero velocity as the character comes to rest.
		glm::vec3 flatVel(m_Velocity.x, 0.0f, m_Velocity.z);
		if (glm::length(flatVel) > 0.15f)
		{
			// Camera convention: heading = (cos yaw, 0, sin yaw), so yaw = atan2(z, x).
			float desiredYaw = glm::degrees(std::atan2(flatVel.z, flatVel.x));
			float diff = ShortestAngleDelta(m_Yaw, desiredYaw);
			float maxTurn = m_Config.turnSpeedDegrees * dt;
			m_Yaw += std::clamp(diff, -maxTurn, maxTurn);
		}

		// --- Vertical ---------------------------------------------------------
		m_VelocityY += m_Config.gravity * dt;
		m_Position.y += m_VelocityY * dt;

		// Support height = terrain, or the top of any object we're standing on,
		// whichever is higher. This is what makes crates walkable: previously
		// the only floor in existence was the heightmap, so a box top was not a
		// surface and stepping onto one dropped you straight through it.
		//
		// While RISING, object tops are ignored (maxRise 0 still admits a
		// surface at exactly foot level, and anything above is excluded) so a
		// jump isn't cut short by the ledge you're jumping toward.
		float groundY = m_Ground ? m_Ground(m_Position.x, m_Position.z) : 0.0f;
		if (m_Support)
		{
			const float maxRise = (m_VelocityY > 0.0f) ? 0.0f : m_Config.stepHeight;
			groundY = std::max(groundY, m_Support(m_Position, m_Config.radius, maxRise));
		}

		m_Grounded = false;

		if (m_Position.y <= groundY)
		{
			m_Position.y = groundY;
			m_VelocityY  = 0.0f;
			m_Grounded   = true;
		}
		else if (m_VelocityY <= 0.0f && (m_Position.y - groundY) <= m_Config.groundSnapDistance)
		{
			// Descending and hovering just above the surface: stick to it.
			//
			// This is the downhill fix. Walking down a slope, the ground drops
			// away faster than gravity pulls you into it for the first frames,
			// so a strict "y <= groundY" test reports airborne on and off the
			// whole way down — which blocks jumping and makes the camera bob.
			// Gated on descending so it can never eat a jump the instant it
			// starts (positive velocityY is left alone).
			m_Position.y = groundY;
			m_VelocityY  = 0.0f;
			m_Grounded   = true;
		}

		// --- Jump, with forgiveness -------------------------------------------
		m_TimeSinceGrounded = m_Grounded ? 0.0f : (m_TimeSinceGrounded + dt);

		// Remember a press so it can fire on landing rather than being dropped.
		//
		// Buffer on the RISING edge only. PawnInput::jumpPressed is documented
		// as an edge, but detecting it here too means a caller that passes the
		// key's held state can't turn the buffer into an auto-bunny-hop: it
		// would re-arm every frame and re-fire the instant you touched down.
		// Cheap insurance, and holding jump should never out-perform tapping it
		// in a game about moving deliberately.
		const bool jumpRisingEdge = input.jumpPressed && !m_JumpHeldLastFrame;
		m_JumpHeldLastFrame = input.jumpPressed;

		if (jumpRisingEdge)
			m_JumpBufferedFor = 0.0f;
		else if (m_JumpBufferedFor >= 0.0f)
			m_JumpBufferedFor += dt;

		const bool hasBufferedJump = (m_JumpBufferedFor >= 0.0f
			&& m_JumpBufferedFor <= m_Config.jumpBufferTime);
		const bool canJump = (m_TimeSinceGrounded <= m_Config.coyoteTime);

		// Jump doubles as the climb key: if there's a ledge in front, pull up
		// onto it instead of hopping. Checked BEFORE the ordinary jump so that
		// facing a wall always mantles — bouncing uselessly off a ledge you can
		// obviously reach is the single most annoying thing a climb system can
		// do. Airborne mantling is allowed on purpose: jumping at a wall and
		// catching it is the move players will reach for.
		const bool standing = (m_StanceTarget == Stance::Standing);
		if (hasBufferedJump && standing && m_LedgeProbe)
		{
			glm::vec3 ledge(0.0f);
			glm::vec3 facing = GetForward();

			if (m_LedgeProbe(m_Position, facing, m_Config.climbReach,
				m_Config.climbMaxHeight, m_Config.climbHeadroom, ledge))
			{
				m_Climbing = true;
				m_ClimbProgress = 0.0f;
				m_ClimbStart = m_Position;
				m_ClimbEnd = ledge;
				m_VelocityY = 0.0f;
				m_JumpBufferedFor = -1.0f;
				return;
			}
		}

		// Crouch/prone suppress the jump: they're a commitment to a low stance,
		// and hopping out of cover on the same key would undercut it.
		if (hasBufferedJump && canJump && standing)
		{
			m_VelocityY = m_Config.jumpSpeed;
			m_Grounded = false;

			// Consume both, or the same press would re-trigger next frame and
			// coyote time would grant a second jump in mid-air.
			m_JumpBufferedFor = -1.0f;
			m_TimeSinceGrounded = m_Config.coyoteTime + 1.0f;
		}
	}

} // namespace Nightbloom
