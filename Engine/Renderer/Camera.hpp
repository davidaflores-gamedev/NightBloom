//------------------------------------------------------------------------------
// Camera.hpp
//
//------------------------------------------------------------------------------
// Camera.hpp
//
// Free-fly camera for scene navigation
//------------------------------------------------------------------------------
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Nightbloom
{
	class Camera
	{
	public:
		Camera() = default;

		// Setup
		void SetPosition(const glm::vec3& position) { m_Position = position; m_ViewDirty = true; }
		void SetRotation(float yaw, float pitch);
		void SetPerspective(float fovDegrees, float aspect, float nearPlane, float farPlane);

		// Reverse-Z projection (better depth precision for distant objects)
		// Maps near plane to 1.0, far plane to 0.0
		// Use with VK_COMPARE_OP_GREATER_OR_EQUAL and clear depth to 0.0
		void SetPerspectiveReverseZ(float fovDegrees, float aspect, float nearPlane, float farPlane);

		// Infinite far plane reverse-Z (best precision, no far clip)
		void SetPerspectiveInfiniteReverseZ(float fovDegrees, float aspect, float nearPlane);

		bool IsReverseZ() const { return m_UseReverseZ; }

		// Movement
		void MoveForward(float amount);
		void MoveRight(float amount);
		void MoveUp(float amount);
		void Rotate(float yawDelta, float pitchDelta);

		// Free-fly update (call each frame)
		void Update(float deltaTime);

		// Input state (set from input system)
		void SetMovementInput(const glm::vec3& input) { m_MovementInput = input; }
		void SetRotationInput(const glm::vec2& input) { m_RotationInput = input; }

		// Getters
		const glm::mat4& GetViewMatrix();
		const glm::mat4& GetProjectionMatrix() const { return m_Projection; }
		const glm::vec3& GetPosition() const { return m_Position; }
		const glm::vec3& GetForward() const { return m_Forward; }
		const glm::vec3& GetRight() const { return m_Right; }
		const glm::vec3& GetUp() const { return m_Up; }
		float GetYaw() const { return m_Yaw; }
		float GetPitch() const { return m_Pitch; }

		// Settings
		float moveSpeed = 5.0f;
		float lookSensitivity = 0.1f;
		float sprintMultiplier = 2.5f;
		bool isSprinting = false;

	private:
		void UpdateVectors();

		// Transform
		glm::vec3 m_Position = glm::vec3(0.0f, 0.0f, 3.0f);
		float m_Yaw = -90.0f;    // Facing -Z by default
		float m_Pitch = 0.0f;

		// Derived vectors
		glm::vec3 m_Forward = glm::vec3(0.0f, 0.0f, -1.0f);
		glm::vec3 m_Right = glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec3 m_Up = glm::vec3(0.0f, 1.0f, 0.0f);

		// Matrices
		glm::mat4 m_View = glm::mat4(1.0f);
		glm::mat4 m_Projection = glm::mat4(1.0f);
		bool m_ViewDirty = true;
		bool m_UseReverseZ = false;

		// Input accumulation
		glm::vec3 m_MovementInput = glm::vec3(0.0f);  // x=right, y=up, z=forward
		glm::vec2 m_RotationInput = glm::vec2(0.0f);  // x=yaw, y=pitch

	};

} // namespace Nightbloom