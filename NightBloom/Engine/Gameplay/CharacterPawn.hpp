//------------------------------------------------------------------------------
// CharacterPawn.hpp
//
// Kinematic on-foot pawn: walks in the camera plane, gravity + snap-to-ground,
// jump. Lifted from GameApplication's original UpdateCharacter so the walking
// logic is reusable (Game runtime today; editor play-mode / other prototypes
// later). Ground height is injected as a sampler so the pawn stays decoupled
// from TerrainSystem (and works with a flat floor when there's no terrain).
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Gameplay/Pawn.hpp"

#include <functional>

namespace Nightbloom
{
	class CharacterPawn : public Pawn
	{
	public:
		// Feel tunables — expect to tweak these live. Defaults match the values
		// GameApplication shipped with.
		struct Config
		{
			float walkSpeed = 6.0f;    // m/s
			float sprintMul = 2.0f;
			float gravity   = -20.0f;  // m/s^2 (snappier than 9.8)
			float jumpSpeed = 8.0f;    // m/s initial jump velocity
			float eyeHeight = 1.7f;    // camera focus height above the feet
		};

		// Returns the ground height (world Y) at a world (x, z). Injected by the
		// app so CharacterPawn doesn't depend on TerrainSystem. If unset, the
		// pawn treats y = 0 as the floor.
		using GroundSampler = std::function<float(float x, float z)>;

		void SetGroundSampler(GroundSampler sampler) { m_Ground = std::move(sampler); }

		Config&       GetConfig()       { return m_Config; }
		const Config& GetConfig() const { return m_Config; }

		bool IsGrounded() const { return m_Grounded; }

		void Update(const PawnInput& input, float dt) override;

		glm::vec3 GetCameraFocus() const override
		{
			return m_Position + glm::vec3(0.0f, m_Config.eyeHeight, 0.0f);
		}

	private:
		Config        m_Config;
		GroundSampler m_Ground;
		float         m_VelocityY = 0.0f;
		bool          m_Grounded  = false;
	};

} // namespace Nightbloom
