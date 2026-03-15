//------------------------------------------------------------------------------
// Camera.cpp
//
// Implementation of free-fly camera
//------------------------------------------------------------------------------

#include "Engine/Renderer/Camera.hpp"
#include <algorithm>

namespace Nightbloom
{
	void Camera::SetRotation(float yaw, float pitch)
	{
		m_Yaw = yaw;
		m_Pitch = glm::clamp(pitch, -89.0f, 89.0f);
		UpdateVectors();
	}

	void Camera::SetPerspective(float fovDegrees, float aspect, float nearPlane, float farPlane)
	{
		m_Projection = glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
		m_Projection[1][1] *= -1.0f;  // Vulkan Y-flip
		m_UseReverseZ = false;
	}

	void Camera::SetPerspectiveReverseZ(float fovDegrees, float aspect, float nearPlane, float farPlane)
	{
		// Reverse-Z perspective projection
		// Maps: near plane -> 1.0, far plane -> 0.0
		// This provides much better depth precision for distant objects

		float fovRad = glm::radians(fovDegrees);
		float tanHalfFov = tan(fovRad / 2.0f);

		m_Projection = glm::mat4(0.0f);

		// Standard perspective terms
		m_Projection[0][0] = 1.0f / (aspect * tanHalfFov);
		m_Projection[1][1] = -1.0f / tanHalfFov;  // Vulkan Y-flip built in

		// Reverse-Z depth mapping:
		// Standard: z' = (far * z - far * near) / (z * (far - near))
		// Reverse:  z' = near / z  (for infinite far) or
		//           z' = (near * far) / (z * (far - near)) - near / (far - near) + 1
		//           which simplifies the depth precision distribution

		// For finite far plane reverse-Z:
		// z maps [near, far] to [1, 0]
		m_Projection[2][2] = nearPlane / (nearPlane - farPlane);
		m_Projection[3][2] = (nearPlane * farPlane) / (nearPlane - farPlane);
		m_Projection[2][3] = -1.0f;

		m_UseReverseZ = true;
	}

	void Camera::SetPerspectiveInfiniteReverseZ(float fovDegrees, float aspect, float nearPlane)
	{
		// Infinite far plane reverse-Z projection
		// This is the "gold standard" for depth precision
		// Near plane maps to 1.0, infinity maps to 0.0

		float fovRad = glm::radians(fovDegrees);
		float tanHalfFov = tan(fovRad / 2.0f);

		m_Projection = glm::mat4(0.0f);

		// Standard perspective terms
		m_Projection[0][0] = 1.0f / (aspect * tanHalfFov);
		m_Projection[1][1] = -1.0f / tanHalfFov;  // Vulkan Y-flip built in

		// Infinite reverse-Z:
		// As far -> infinity: z' = near / z
		// This gives perfect logarithmic precision distribution
		m_Projection[2][2] = 0.0f;
		m_Projection[3][2] = nearPlane;
		m_Projection[2][3] = -1.0f;

		m_UseReverseZ = true;
	}

	void Camera::MoveForward(float amount)
	{
		m_Position += m_Forward * amount;
		m_ViewDirty = true;
	}

	void Camera::MoveRight(float amount)
	{
		m_Position += m_Right * amount;
		m_ViewDirty = true;
	}

	void Camera::MoveUp(float amount)
	{
		m_Position += glm::vec3(0.0f, 1.0f, 0.0f) * amount;  // World up
		m_ViewDirty = true;
	}

	void Camera::Rotate(float yawDelta, float pitchDelta)
	{
		m_Yaw += yawDelta;
		m_Pitch = glm::clamp(m_Pitch + pitchDelta, -89.0f, 89.0f);
		UpdateVectors();
	}

	void Camera::Update(float deltaTime)
	{
		// Apply rotation from accumulated input
		if (m_RotationInput.x != 0.0f || m_RotationInput.y != 0.0f)
		{
			Rotate(m_RotationInput.x * lookSensitivity,
				m_RotationInput.y * lookSensitivity);
			m_RotationInput = glm::vec2(0.0f);  // Reset
		}

		// Apply movement from accumulated input
		if (glm::length(m_MovementInput) > 0.001f)
		{
			float speed = moveSpeed * (isSprinting ? sprintMultiplier : 1.0f) * deltaTime;

			MoveForward(m_MovementInput.z * speed);
			MoveRight(m_MovementInput.x * speed);
			MoveUp(m_MovementInput.y * speed);

			m_MovementInput = glm::vec3(0.0f);  // Reset
		}
	}

	const glm::mat4& Camera::GetViewMatrix()
	{
		if (m_ViewDirty)
		{
			m_View = glm::lookAt(m_Position, m_Position + m_Forward, glm::vec3(0.0f, 1.0f, 0.0f));
			m_ViewDirty = false;
		}
		return m_View;
	}

	void Camera::UpdateVectors()
	{
		glm::vec3 forward;
		forward.x = cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
		forward.y = sin(glm::radians(m_Pitch));
		forward.z = sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
		m_Forward = glm::normalize(forward);

		// Recalculate right and up
		m_Right = glm::normalize(glm::cross(m_Forward, glm::vec3(0.0f, 1.0f, 0.0f)));
		m_Up = glm::normalize(glm::cross(m_Right, m_Forward));

		m_ViewDirty = true;
	}

} // namespace Nightbloom