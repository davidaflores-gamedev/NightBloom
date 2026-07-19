//------------------------------------------------------------------------------
// ThirdPersonCameraRig.cpp
//------------------------------------------------------------------------------
#include "Engine/Gameplay/ThirdPersonCameraRig.hpp"
#include "Engine/Renderer/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Nightbloom
{
	namespace
	{
		// Same spherical mapping Camera uses internally, so the rig and the camera
		// agree on what a (yaw, pitch) pair points at.
		glm::vec3 DirFromYawPitch(float yawDeg, float pitchDeg)
		{
			const float y = glm::radians(yawDeg);
			const float p = glm::radians(pitchDeg);
			return glm::normalize(glm::vec3(std::cos(y) * std::cos(p),
			                                std::sin(p),
			                                std::sin(y) * std::cos(p)));
		}
	}

	void ThirdPersonCameraRig::SetYawPitch(float yawDegrees, float pitchDegrees)
	{
		m_Yaw   = yawDegrees;
		m_Pitch = std::clamp(pitchDegrees, m_Config.minPitch, m_Config.maxPitch);
	}

	void ThirdPersonCameraRig::ApplyLook(const glm::vec2& lookDelta)
	{
		m_Yaw += lookDelta.x * m_Config.lookSensitivity;
		m_Pitch = std::clamp(m_Pitch + lookDelta.y * m_Config.lookSensitivity,
		                     m_Config.minPitch, m_Config.maxPitch);
	}

	glm::vec3 ThirdPersonCameraRig::GetFlatForward() const
	{
		glm::vec3 f = DirFromYawPitch(m_Yaw, 0.0f);
		f.y = 0.0f;
		return glm::normalize(f);
	}

	glm::vec3 ThirdPersonCameraRig::GetFlatRight() const
	{
		// Match Camera: right = cross(forward, worldUp).
		return glm::normalize(glm::cross(GetFlatForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
	}

	void ThirdPersonCameraRig::UpdateCamera(Camera& camera, const glm::vec3& focus,
	                                        const std::function<float(float, float)>* groundSampler) const
	{
		const glm::vec3 fwd   = DirFromYawPitch(m_Yaw, m_Pitch);
		const glm::vec3 pivot = focus + glm::vec3(0.0f, m_Config.pivotHeight, 0.0f);
		glm::vec3 camPos      = pivot - fwd * m_Config.distance;

		// Keep the boom above the ground so it doesn't sink into a hill behind us.
		if (groundSampler && *groundSampler)
		{
			const float minY = (*groundSampler)(camPos.x, camPos.z) + m_Config.groundClearance;
			if (camPos.y < minY)
				camPos.y = minY;
		}

		// Camera looks along (yaw, pitch), i.e. from camPos back through the pivot.
		camera.SetPosition(camPos);
		camera.SetRotation(m_Yaw, m_Pitch);
	}

} // namespace Nightbloom
