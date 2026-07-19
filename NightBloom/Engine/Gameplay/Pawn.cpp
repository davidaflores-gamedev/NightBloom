//------------------------------------------------------------------------------
// Pawn.cpp
//------------------------------------------------------------------------------
#include "Engine/Gameplay/Pawn.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Nightbloom
{
	glm::vec3 Pawn::GetForward() const
	{
		const float y = glm::radians(m_Yaw);
		return glm::vec3(std::cos(y), 0.0f, std::sin(y));
	}

	glm::vec3 Pawn::GetRight() const
	{
		// Matches Camera/ThirdPersonCameraRig: right = cross(forward, worldUp).
		return glm::cross(GetForward(), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::mat4 Pawn::GetVisualTransform() const
	{
		// translate(feet) * rotateY(heading). The mesh's local "front" is +X, and
		// rotateY(-yaw) maps +X onto the heading direction (cos yaw, 0, sin yaw),
		// so the body visibly turns to face where it moves. Sign is cosmetic for a
		// symmetric placeholder box — revisit when a real character mesh lands.
		return glm::translate(glm::mat4(1.0f), m_Position) *
		       glm::rotate(glm::mat4(1.0f), glm::radians(-m_Yaw), glm::vec3(0.0f, 1.0f, 0.0f));
	}

} // namespace Nightbloom
