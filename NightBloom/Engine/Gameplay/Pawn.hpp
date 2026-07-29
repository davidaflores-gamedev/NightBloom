//------------------------------------------------------------------------------
// Pawn.hpp
//
// The pawn abstraction (Phase 1 of the pawn/vehicle arc — see .claude/CLAUDE.md
// "Current Session Status" and the PACKAGING_ROADMAP).
//
// A Pawn is a possessable, controllable entity: it owns a world transform and a
// simulation step driven by a *device-neutral* PawnInput. It knows nothing about
// rendering, the window, or the input backend — the application fills a PawnInput
// once per frame (from keyboard/mouse/gamepad) and hands it to whichever pawn is
// currently possessed. This is what lets a future VehiclePawn drop in beside
// CharacterPawn with no controller rewrite.
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Math/MathUtils.hpp"   // ShortestAngleDelta, used by pawn facing

#include <glm/glm.hpp>

namespace Nightbloom
{
	//--------------------------------------------------------------------------
	// PawnInput — one frame of intent, already resolved from the input devices.
	//
	// Movement is expressed camera-relative (move + the flattened camera basis)
	// so any pawn can choose to honor it (CharacterPawn walks in the camera plane)
	// or ignore it (a VehiclePawn steers by its own heading). Vehicle fields are
	// present now so the struct is stable for Phase 2 — CharacterPawn ignores them.
	//--------------------------------------------------------------------------
	struct PawnInput
	{
		// Movement intent. x = strafe (right), y = forward. Range [-1, 1].
		glm::vec2 move = glm::vec2(0.0f);

		// Flattened (y = 0), normalized camera basis for camera-relative movement.
		// The pawn maps move.y -> cameraForward, move.x -> cameraRight.
		glm::vec3 cameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
		glm::vec3 cameraRight   = glm::vec3(1.0f, 0.0f, 0.0f);

		bool sprint          = false;  // held
		bool crouch          = false;  // held — crouch stance (CharacterPawn)
		bool prone           = false;  // held/toggled — prone crawl; overrides crouch
		bool jumpPressed     = false;  // edge — down this frame only
		bool interactPressed = false;  // edge — enter/exit vehicle (Phase 3)

		// --- Vehicle intents (Phase 2; CharacterPawn ignores these) ------------
		float throttle = 0.0f;         // [0, 1]
		float brake    = 0.0f;         // [0, 1]
		float steer    = 0.0f;         // [-1, 1]
		bool  shiftUpPressed   = false;
		bool  shiftDownPressed = false;
	};

	//--------------------------------------------------------------------------
	// Pawn — abstract base. Simulation only; the app wires it to a scene.
	//--------------------------------------------------------------------------
	class Pawn
	{
	public:
		virtual ~Pawn() = default;

		// Possession lifecycle. Possession lives in the application (a Pawn*),
		// so swapping which pawn is active is just a pointer change + these hooks.
		virtual void OnPossess() {}
		virtual void OnUnpossess() {}

		// Advance the simulation by dt using this frame's intent.
		virtual void Update(const PawnInput& input, float dt) = 0;

		// Feet / base world position.
		const glm::vec3& GetPosition() const { return m_Position; }
		void SetPosition(const glm::vec3& p) { m_Position = p; }

		// Facing yaw in degrees, matching Camera's convention
		// (heading direction = (cos yaw, 0, sin yaw)).
		float GetYaw() const { return m_Yaw; }
		void  SetYaw(float yawDegrees) { m_Yaw = yawDegrees; }

		// The point the third-person camera orbits (typically head/chest height).
		// Defaults to the feet; CharacterPawn raises it to eye height.
		virtual glm::vec3 GetCameraFocus() const { return m_Position; }

		// Boom length the third-person rig should use for this pawn. Framing is a
		// property of what you're driving (a bike wants a longer boom than a person),
		// so it lives here and the app pushes it into the rig on possession.
		virtual float GetCameraDistance() const { return 5.0f; }

		// Heading direction on the ground plane, matching the yaw convention above.
		glm::vec3 GetForward() const;
		glm::vec3 GetRight() const;

		// World transform for the visible body/mesh: translation + yaw. Subclasses
		// may add pitch/roll tilt (a VehiclePawn leaning into a turn). The caller
		// multiplies in any mesh-local offset/scale for its placeholder geometry.
		virtual glm::mat4 GetVisualTransform() const;

	protected:
		glm::vec3 m_Position = glm::vec3(0.0f);  // feet
		float     m_Yaw      = 0.0f;             // degrees
	};

} // namespace Nightbloom
