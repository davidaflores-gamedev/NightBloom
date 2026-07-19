//------------------------------------------------------------------------------
// ThirdPersonCameraRig.hpp
//
// A third-person orbit/boom camera. Owns its own yaw/pitch (fed the frame's look
// delta) and a boom distance, and drives an existing Camera: it places the camera
// behind a focus point and aims it back at that point. Both the character and a
// future vehicle pawn use the same rig.
//
// It also exposes the flattened camera basis so the possessed pawn can move
// camera-relative (walk where you're looking).
//------------------------------------------------------------------------------
#pragma once

#include <glm/glm.hpp>
#include <functional>

namespace Nightbloom
{
	class Camera;

	class ThirdPersonCameraRig
	{
	public:
		struct Config
		{
			float distance         = 5.0f;    // boom length behind the focus
			float lookSensitivity  = 0.1f;     // matches Camera's free-fly sensitivity
			float minPitch         = -60.0f;   // degrees; look-down limit
			float maxPitch         = 75.0f;    // degrees; look-up limit
			float pivotHeight      = 0.0f;     // extra height above the focus point
			float groundClearance  = 0.5f;     // keep the camera this far above terrain
		};

		Config&       GetConfig()       { return m_Config; }
		const Config& GetConfig() const { return m_Config; }

		void  SetYawPitch(float yawDegrees, float pitchDegrees);
		float GetYaw() const   { return m_Yaw; }
		float GetPitch() const { return m_Pitch; }

		// Accumulate this frame's look delta (raw, in the same units the free-fly
		// camera consumed — scaled internally by lookSensitivity).
		void ApplyLook(const glm::vec2& lookDelta);

		// Flattened (y = 0), normalized basis for camera-relative movement.
		glm::vec3 GetFlatForward() const;
		glm::vec3 GetFlatRight() const;

		// Position `camera` orbiting `focus` at the current yaw/pitch/distance and
		// aim it back at the focus. `groundSampler` (nullable) keeps the boom from
		// clipping under the terrain.
		void UpdateCamera(Camera& camera, const glm::vec3& focus,
		                   const std::function<float(float, float)>* groundSampler = nullptr) const;

	private:
		Config m_Config;
		float  m_Yaw   = 0.0f;
		float  m_Pitch = -15.0f;  // start looking slightly down at the character
	};

} // namespace Nightbloom
