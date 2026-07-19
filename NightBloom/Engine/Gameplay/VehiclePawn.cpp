//------------------------------------------------------------------------------
// VehiclePawn.cpp
//------------------------------------------------------------------------------
#include "Engine/Gameplay/VehiclePawn.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Nightbloom
{
	namespace
	{
		// Frame-rate independent exponential approach toward `target`.
		float ApproachExp(float current, float target, float rate, float dt)
		{
			const float t = 1.0f - std::exp(-rate * dt);
			return current + (target - current) * t;
		}
	}

	glm::vec3 VehiclePawn::GetSeatPosition() const
	{
		return m_Position + glm::vec3(0.0f, m_Config.seatHeight, 0.0f);
	}

	glm::vec3 VehiclePawn::GetDismountPosition(float side) const
	{
		glm::vec3 p = m_Position + GetRight() * (1.4f * side);
		p.y = m_Ground ? m_Ground(p.x, p.z) : 0.0f;
		return p;
	}

	void VehiclePawn::OnPossess()
	{
		// Nothing to restore yet — the bike keeps its momentum between rides, which
		// is what you want if you hop off while it's still drifting.
	}

	void VehiclePawn::OnUnpossess()
	{
		// Cut the throttle when the rider leaves so an unpossessed bike coasts to a
		// stop instead of driving off on the last frame's input.
		m_Speed = 0.0f;
	}

	void VehiclePawn::Update(const PawnInput& input, float dt)
	{
		// --- Longitudinal speed ------------------------------------------------
		// Throttle accelerates along the heading, brake decelerates and then backs
		// up, and with neither held the bike coasts down through drag.
		if (input.throttle > 0.01f)
		{
			m_Speed += m_Config.acceleration * input.throttle * dt;
		}
		else if (input.brake > 0.01f)
		{
			m_Speed -= m_Config.brakeDecel * input.brake * dt;
		}
		else
		{
			const float drag = m_Config.coastDrag * dt;
			if (m_Speed > drag)       m_Speed -= drag;
			else if (m_Speed < -drag) m_Speed += drag;
			else                      m_Speed = 0.0f;
		}
		m_Speed = std::clamp(m_Speed, -m_Config.maxReverse, m_Config.maxSpeed);

		// --- Steering ----------------------------------------------------------
		// Steering authority ramps in with speed: a parked bike can't pivot in place,
		// and the turn reverses when reversing (like backing up any real vehicle).
		const float speedFactor = std::clamp(std::abs(m_Speed) / m_Config.turnSpeedRef, 0.0f, 1.0f);
		const float dirSign     = (m_Speed < 0.0f) ? -1.0f : 1.0f;
		m_Yaw += input.steer * m_Config.turnRate * speedFactor * dirSign * dt;

		// --- Translation -------------------------------------------------------
		const glm::vec3 fwd = GetForward();
		m_Position.x += fwd.x * m_Speed * dt;
		m_Position.z += fwd.z * m_Speed * dt;

		// --- Hover -------------------------------------------------------------
		// Spring-damper toward (ground + rideHeight). No gravity term: the hover
		// field *is* the vertical force, so the bike holds its height exactly rather
		// than sagging by gravity/stiffness, and drops smoothly off a ledge instead
		// of falling.
		const float groundY = m_Ground ? m_Ground(m_Position.x, m_Position.z) : 0.0f;
		const float targetY = groundY + m_Config.hoverHeight;
		const float accelY  = (targetY - m_Position.y) * m_Config.hoverStiffness
		                    - m_VelocityY * m_Config.hoverDamping;
		m_VelocityY  += accelY * dt;
		m_Position.y += m_VelocityY * dt;

		// --- Cosmetic bank + terrain-following pitch ---------------------------
		const float targetBank = -input.steer * m_Config.maxBank * speedFactor;
		m_Bank = ApproachExp(m_Bank, targetBank, m_Config.bankLerp, dt);

		float targetPitch = 0.0f;
		if (m_Ground)
		{
			// Read the slope from ground samples fore and aft of the bike; nose-up on
			// an incline. Cheap, and enough to sell "following the terrain".
			const glm::vec3 front = m_Position + fwd * m_Config.slopeProbe;
			const glm::vec3 back  = m_Position - fwd * m_Config.slopeProbe;
			const float dy = m_Ground(front.x, front.z) - m_Ground(back.x, back.z);
			targetPitch = glm::degrees(std::atan2(dy, 2.0f * m_Config.slopeProbe));
			targetPitch = std::clamp(targetPitch, -m_Config.maxSlopePitch, m_Config.maxSlopePitch);
		}
		m_Pitch = ApproachExp(m_Pitch, targetPitch, m_Config.bankLerp, dt);
	}

	glm::mat4 VehiclePawn::GetVisualTransform() const
	{
		// Base yaw transform (mesh front is +X, per Pawn::GetVisualTransform), then
		// pitch and roll in the bike's local frame. With front = +X and up = +Y, the
		// local right axis is +Z, so pitch rotates about Z and roll about X.
		return glm::translate(glm::mat4(1.0f), m_Position)
		     * glm::rotate(glm::mat4(1.0f), glm::radians(-m_Yaw),  glm::vec3(0.0f, 1.0f, 0.0f))
		     * glm::rotate(glm::mat4(1.0f), glm::radians(m_Pitch), glm::vec3(0.0f, 0.0f, 1.0f))
		     * glm::rotate(glm::mat4(1.0f), glm::radians(m_Bank),  glm::vec3(1.0f, 0.0f, 0.0f));
	}

} // namespace Nightbloom
