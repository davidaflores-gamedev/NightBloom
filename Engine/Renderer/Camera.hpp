// Engine/Camera/Camera.hpp
#pragma once
#include <glm/glm.hpp>

namespace Nightbloom
{
	class Camera {
	public:
		Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f));

		// Get matrices
		glm::mat4 GetViewMatrix() const;
		glm::mat4 GetProjectionMatrix(float aspectRatio) const;

		// Simple controls
		void MoveForward(float distance);
		void MoveRight(float distance);
		void MoveUp(float distance);
		void Rotate(float yaw, float pitch);

		// Direct setters for testing
		void SetPosition(const glm::vec3& pos) { m_Position = pos; }
		void LookAt(const glm::vec3& target);

	private:
		glm::vec3 m_Position;
		glm::vec3 m_Front = glm::vec3(0.0f, 0.0f, -1.0f);
		glm::vec3 m_Up = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 m_Right = glm::vec3(1.0f, 0.0f, 0.0f);

		float m_Yaw = -90.0f;
		float m_Pitch = 0.0f;
		float m_FOV = 45.0f;
		float m_Near = 0.1f;
		float m_Far = 100.0f;

		void UpdateVectors();
	}
}