//------------------------------------------------------------------------------
// CharacterPawn.cpp
//------------------------------------------------------------------------------
#include "Engine/Gameplay/CharacterPawn.hpp"

#include <cmath>

namespace Nightbloom
{
	void CharacterPawn::Update(const PawnInput& input, float dt)
	{
		// Horizontal wish-direction in the camera-yaw plane (ignore pitch).
		glm::vec3 fwd   = input.cameraForward;  fwd.y   = 0.0f;
		glm::vec3 right = input.cameraRight;    right.y = 0.0f;
		if (glm::dot(fwd, fwd) > 1e-6f)     fwd   = glm::normalize(fwd);
		if (glm::dot(right, right) > 1e-6f) right = glm::normalize(right);

		glm::vec3 wish = fwd * input.move.y + right * input.move.x;
		if (glm::dot(wish, wish) > 1e-6f)
		{
			wish = glm::normalize(wish);
			// Face the travel direction. Camera convention: heading = (cos yaw, 0, sin yaw),
			// so yaw = atan2(z, x).
			m_Yaw = glm::degrees(std::atan2(wish.z, wish.x));
		}

		const float speed = m_Config.walkSpeed * (input.sprint ? m_Config.sprintMul : 1.0f);
		m_Position.x += wish.x * speed * dt;
		m_Position.z += wish.z * speed * dt;

		// Gravity, then snap to the ground.
		m_VelocityY += m_Config.gravity * dt;
		m_Position.y += m_VelocityY * dt;

		const float groundY = m_Ground ? m_Ground(m_Position.x, m_Position.z) : 0.0f;
		m_Grounded = false;
		if (m_Position.y <= groundY)
		{
			m_Position.y = groundY;
			m_VelocityY  = 0.0f;
			m_Grounded   = true;
		}

		if (m_Grounded && input.jumpPressed)
			m_VelocityY = m_Config.jumpSpeed;
	}

} // namespace Nightbloom
