//------------------------------------------------------------------------------
// CharacterPawn.hpp
//
// Kinematic on-foot pawn: walks in the camera plane, gravity + snap-to-ground,
// jump, crouch. Ground height is injected as a sampler so the pawn stays
// decoupled from TerrainSystem (and works with a flat floor when there's no
// terrain).
//
// MOVEMENT MODEL (rewritten 2026-07-18). The original version was instantaneous
// in both senses: `m_Yaw = atan2(wish)` snapped the body to a new facing in a
// single frame, and position took full speed the moment a key went down and
// dead-stopped the moment it came up. That reads as teleporting rather than
// moving. Now the pawn keeps a horizontal VELOCITY that accelerates toward the
// wish direction, and a yaw that SLEWS toward the travel direction at a finite
// turn rate.
//
// This is not scaffolding that animation will replace — it is what animation
// will read. A blend tree wants a speed scalar to blend walk/run and a turn
// rate to drive lean; both now exist as real state instead of being implied by
// key-down. Adding models later changes GetVisualTransform's consumers, not
// this.
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Gameplay/Pawn.hpp"

#include <functional>

namespace Nightbloom
{
	class CharacterPawn : public Pawn
	{
	public:
		// Feel tunables — expect to tweak these live.
		struct Config
		{
			float walkSpeed = 6.0f;    // m/s
			float sprintMul = 2.0f;
			float gravity   = -20.0f;  // m/s^2 (snappier than 9.8)
			float jumpSpeed = 8.0f;    // m/s initial jump velocity
			float eyeHeight = 1.7f;    // standing camera focus height above the feet

			// --- Momentum ------------------------------------------------------
			// Acceleration is what stops movement feeling like teleportation.
			// Deceleration is deliberately higher than acceleration: ramping up
			// slowly reads as weight, but stopping slowly reads as ice, and a
			// stealth game needs precise placement.
			float acceleration = 45.0f;  // m/s^2 toward the wish velocity
			float deceleration = 60.0f;  // m/s^2 toward rest when no input

			// Fraction of ground control retained mid-air. Full air control feels
			// weightless; zero is punishing when jumping onto ledges.
			float airControl = 0.35f;

			// --- Turning -------------------------------------------------------
			// How fast facing slews toward the travel direction. High enough to
			// feel responsive, low enough that a 180 reads as a turn rather than
			// a snap. At 720 deg/s a full reversal takes a quarter second.
			float turnSpeedDegrees = 720.0f;

			// --- Stances (stand / crouch / prone) ------------------------------
			// Three postures, each slower + lower + harder to see than the last.
			// Prone unlocks crawling under low cover and hiding in short grass;
			// the cost is you're barely mobile.
			float crouchSpeedMul   = 0.45f;  // slower, quieter
			float crouchEyeHeight  = 1.0f;   // lowered camera focus
			float crouchHeightMul  = 0.55f;  // visual/collision height fraction

			float proneSpeedMul    = 0.22f;  // a crawl
			float proneEyeHeight   = 0.45f;  // eyes near the dirt
			float proneHeightMul   = 0.28f;  // very low silhouette

			float stanceBlendSpeed = 7.0f;   // 1/s toward the target stance

			// --- Body (collision) ----------------------------------------------
			float radius = 0.35f;  // horizontal half-width
			float height = 1.8f;   // standing height; scales with crouch

			// Tallest ledge you can walk straight up without jumping. Splits
			// "wall" from "step": at or below this, you rise onto it; above, it
			// blocks. Also the tolerance that lets you cross terrain seams and
			// low kerbs without catching.
			float stepHeight = 0.45f;

			// --- Climbing ------------------------------------------------------
			// Mantle: pull up onto a ledge too tall to step over. This is what
			// makes a walled camp a 3D space instead of a maze — walls become
			// routes, and choosing one is a plan rather than a wait.
			//
			// The band is deliberately generous at the top: a ledge you can
			// obviously reach but the code refuses is far more frustrating than
			// one you can reach slightly implausibly.
			float climbMaxHeight = 2.6f;   // tallest ledge you can pull onto
			float climbReach = 1.0f;   // how far ahead a ledge may be
			float climbDuration = 0.55f;  // seconds the pull-up takes
			float climbHeadroom = 1.2f;   // clear space needed above the ledge

			// --- Jump forgiveness ----------------------------------------------
			// Coyote time: you may still jump for this long AFTER leaving the
			// ground. Without it, walking downhill is unjumpable — the ground
			// falls away, gravity makes you technically airborne every frame,
			// and the grounded check refuses a jump the player has every right
			// to expect.
			float coyoteTime = 0.12f;  // seconds

			// Jump buffer: a jump pressed up to this long BEFORE landing fires
			// on touchdown instead of being swallowed. Covers pressing jump a
			// few frames early while still descending.
			float jumpBufferTime = 0.15f;  // seconds

			// Ground snap: while descending and within this distance of the
			// ground, stick to it instead of free-falling. This is what stops
			// the micro-airborne stutter on downhill slopes at the source —
			// coyote time forgives it, this prevents it.
			float groundSnapDistance = 0.40f;  // metres
		};

		// Returns the ground height (world Y) at a world (x, z). Injected by the
		// app so CharacterPawn doesn't depend on TerrainSystem. If unset, the
		// pawn treats y = 0 as the floor.
		using GroundSampler = std::function<float(float x, float z)>;

		// Pushes a desired FEET position out of world geometry and returns the
		// corrected one. Injected for the same reason as the ground sampler:
		// the pawn stays ignorant of Scene. Unset means no world collision
		// (terrain only), which is the correct fallback for an empty scene.
		using CollisionResolver =
			std::function<glm::vec3(const glm::vec3& feetPosition, float radius, float height,
				float stepHeight)>;

		// Highest world-geometry surface the character can stand on here, or a
		// very negative number if none. Kept separate from GroundSampler because
		// terrain is the app's business and object tops are the scene's; the
		// pawn simply stands on whichever is higher. Unset means terrain-only,
		// which is the correct fallback for a scene with no geometry.
		using SupportSampler =
			std::function<float(const glm::vec3& feetPosition, float radius, float maxRise)>;

		// Finds a ledge to mantle onto from `feet` looking along `facing`.
		// Returns true and fills `outLedge` (the world position to end up
		// standing at) if there's one within reach. Injected like the others so
		// the pawn stays free of Scene; the app answers with a downward probe
		// over the geometry in front.
		using LedgeProbe = std::function<bool(const glm::vec3& feet, const glm::vec3& facing,
			float reach, float maxHeight, float headroom, glm::vec3& outLedge)>;

		void SetGroundSampler(GroundSampler sampler) { m_Ground = std::move(sampler); }
		void SetCollisionResolver(CollisionResolver resolver) { m_Collide = std::move(resolver); }
		void SetSupportSampler(SupportSampler sampler) { m_Support = std::move(sampler); }
		void SetLedgeProbe(LedgeProbe probe) { m_LedgeProbe = std::move(probe); }

		// True while a mantle is playing out. Movement and gravity are suspended
		// for its duration — the character is committed, which is the cost that
		// stops climbing being a free dodge.
		bool IsClimbing() const { return m_Climbing; }
		float GetClimbProgress() const { return m_ClimbProgress; }

		Config&       GetConfig()       { return m_Config; }
		const Config& GetConfig() const { return m_Config; }

		enum class Stance { Standing = 0, Crouching = 1, Prone = 2 };

		bool IsGrounded()  const { return m_Grounded; }

		// The posture being *held* this frame (the input target, not the blend).
		Stance GetStance() const { return m_StanceTarget; }
		bool   IsCrouching() const { return m_StanceTarget == Stance::Crouching; }
		bool   IsProne()     const { return m_StanceTarget == Stance::Prone; }

		// Smoothed posture in [0,2] (0 = stand, 1 = crouch, 2 = prone).
		// Continuous rather than discrete so camera/body don't pop, and so an
		// animator or a stealth-profile mapping can read it directly.
		float GetStanceValue() const { return m_StanceValue; }

		// Current horizontal speed in m/s. The scalar an animation blend tree
		// wants; also what a future noise/hearing system should read, since how
		// fast you're actually moving is what makes sound, not which key is down.
		float GetHorizontalSpeed() const;

		// Current stance height as a fraction of standing (1 = stand).
		float GetHeightFraction() const;

		void Update(const PawnInput& input, float dt) override;

		glm::vec3 GetCameraFocus() const override
		{
			return m_Position + glm::vec3(0.0f, CurrentEyeHeight(), 0.0f);
		}

	private:
		float CurrentEyeHeight() const;

		Config            m_Config;
		GroundSampler     m_Ground;
		CollisionResolver m_Collide;
		SupportSampler    m_Support;
		LedgeProbe        m_LedgeProbe;

		glm::vec3 m_Velocity{ 0.0f };   // horizontal only; y handled separately
		float     m_VelocityY   = 0.0f;
		bool      m_Grounded    = false;
		Stance    m_StanceTarget = Stance::Standing;
		float     m_StanceValue = 0.0f;  // 0..2, smoothed

		// Jump forgiveness timers.
		float m_TimeSinceGrounded  = 0.0f;   // for coyote time
		float m_JumpBufferedFor    = -1.0f;  // <0 = no buffered jump
		bool  m_JumpHeldLastFrame  = false;  // rising-edge detect (anti-bunny-hop)

		// Mantle in progress: lerp from where we grabbed to where we end up.
		bool      m_Climbing = false;
		float     m_ClimbProgress = 0.0f;   // 0..1
		glm::vec3 m_ClimbStart{ 0.0f };
		glm::vec3 m_ClimbEnd{ 0.0f };
	};

} // namespace Nightbloom
