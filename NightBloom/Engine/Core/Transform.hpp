#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Nightbloom
{
	// Transform data for objects in the scene
	class Transform
	{
	public:
		// Local space information
		glm::vec3 position = glm::vec3(0.0f);
		glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity quaternion
		glm::vec3 scale = glm::vec3(1.0f);

		// Euler angles for easier editing (in degrees)
		glm::vec3 eulerAngles = glm::vec3(0.0f);

		Transform() = default;

		// Convenience constructor
		Transform(const glm::vec3& pos) : position(pos) {}
		Transform(const glm::vec3& pos, const glm::vec3& euler)
			: position(pos), eulerAngles(euler)
		{
			UpdateRotationFromEuler();
		}

		// Get the model matrix
		glm::mat4 GetModelMatrix() const
		{
			glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
			glm::mat4 r = glm::mat4_cast(rotation);
			glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
			return t * r * s; // order: scale -> rotate -> translate
		}

		// Set rotation from euler angles (Euler)
		void SetEulerAngles(const glm::vec3& euler) {
			eulerAngles = euler;
			UpdateRotationFromEuler();
		}

		// Set rotation from euler angles (degrees)
		void SetEulerAngles(float pitch, float yaw, float roll) {
			eulerAngles = glm::vec3(pitch, yaw, roll);
			UpdateRotationFromEuler();
		}

		// Look at target
		void LookAt(const glm::vec3& target, const glm::vec3& up = glm::vec3(0, 1, 0)) {
			glm::vec3 direction = glm::normalize(target - position);
			rotation = glm::quatLookAt(direction, up);
			UpdateEulerFromRotation();
		}

		// Movement helpers
		glm::vec3 GetForward() const {
			return rotation * glm::vec3(0, 0, -1); // -Z is forward in OpenGL/Vulkan
		}

		glm::vec3 GetRight() const {
			return rotation * glm::vec3(1, 0, 0);
		}

		glm::vec3 GetUp() const {
			return rotation * glm::vec3(0, 1, 0);
		}

	private:
		void UpdateRotationFromEuler() {
			glm::vec3 radians = glm::radians(eulerAngles);
			rotation = glm::quat(radians);
		}

		void UpdateEulerFromRotation() {
			eulerAngles = glm::degrees(glm::eulerAngles(rotation));
		}
	};
}