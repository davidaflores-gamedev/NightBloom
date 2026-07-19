//------------------------------------------------------------------------------
// VehiclePawn.hpp
//
// Hoverbike pawn (Phase 2 of the pawn/vehicle arc). Consumes the throttle/brake/
// steer fields PawnInput reserved in Phase 1 and ignores `move` entirely — a
// vehicle steers by its own heading rather than walking camera-relative.
//
// Hover rather than wheels: a spring-damper holds the bike a fixed height over
// the injected ground sampler, which sidesteps suspension, wheel contacts and
// rigid-body collision (none of which the engine has — Jolt is deliberately not
// vendored yet). The same GroundSampler CharacterPawn uses drives it.
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Gameplay/Pawn.hpp"

#include <functional>

namespace Nightbloom
{
	class VehiclePawn : public Pawn
	{
	public:
		// Feel tunables — expect to tweak these live, same as CharacterPawn::Config.
		struct Config
		{
			// --- Drive ---------------------------------------------------------
			float maxSpeed      = 28.0f;   // m/s forward
			float maxReverse    = 6.0f;    // m/s backward
			float acceleration  = 16.0f;   // m/s^2 under throttle
			float brakeDecel    = 26.0f;   // m/s^2 under brake
			float coastDrag     = 4.0f;    // m/s^2 bleed-off with no input

			// --- Steering ------------------------------------------------------
			float turnRate      = 95.0f;   // deg/s at full steer, scaled by speed
			float turnSpeedRef  = 10.0f;   // m/s at which steering reaches full rate

			// --- Hover ---------------------------------------------------------
			float hoverHeight   = 1.1f;    // ride height above the ground
			float hoverStiffness = 45.0f;  // spring constant pulling to ride height
			float hoverDamping  = 9.0f;    // velocity damping (near-critical)

			// --- Cosmetic ------------------------------------------------------
			float maxBank       = 22.0f;   // deg of roll into a full-speed turn
			float bankLerp      = 6.0f;    // per-second approach rate for bank/pitch
			float slopeProbe    = 1.6f;    // fore/aft distance used to read the slope
			float maxSlopePitch = 35.0f;   // clamp on terrain-following pitch

			// --- Rig -----------------------------------------------------------
			float seatHeight    = 0.75f;   // rider mount point above the bike origin
			float focusHeight   = 1.0f;    // camera focus above the bike origin
			float cameraDistance = 8.5f;   // longer boom than on foot
		};

		using GroundSampler = std::function<float(float x, float z)>;

		void SetGroundSampler(GroundSampler sampler) { m_Ground = std::move(sampler); }

		Config&       GetConfig()       { return m_Config; }
		const Config& GetConfig() const { return m_Config; }

		float GetSpeed() const { return m_Speed; }

		// World-space point a rider sits at (used to park the character mesh on the
		// bike while mounted).
		glm::vec3 GetSeatPosition() const;

		// A point beside the bike to drop a dismounting rider on. `side` is +1 for
		// the bike's right, -1 for its left.
		glm::vec3 GetDismountPosition(float side = 1.0f) const;

		void OnPossess() override;
		void OnUnpossess() override;

		void Update(const PawnInput& input, float dt) override;

		glm::vec3 GetCameraFocus() const override
		{
			return m_Position + glm::vec3(0.0f, m_Config.focusHeight, 0.0f);
		}

		float GetCameraDistance() const override { return m_Config.cameraDistance; }

		// Adds bank (roll) and terrain-following pitch on top of the base
		// translate * yaw transform.
		glm::mat4 GetVisualTransform() const override;

	private:
		Config        m_Config;
		GroundSampler m_Ground;

		float m_Speed     = 0.0f;   // signed, along the heading
		float m_VelocityY = 0.0f;
		float m_Bank      = 0.0f;   // degrees, smoothed
		float m_Pitch     = 0.0f;   // degrees, smoothed
	};

} // namespace Nightbloom
