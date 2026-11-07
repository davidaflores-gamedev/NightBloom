//------------------------------------------------------------------------------
// DebugDraw.hpp
//
// Immediate-mode debug lines: call from anywhere in game code, and they show
// up in the next rendered frame. For hitboxes, AI vision, collision shapes,
// paths, velocity vectors: anything you'd otherwise squint at in a log.
//
//   DebugDraw::Capsule(a, b, 0.3f, DebugDraw::Red);
//   DebugDraw::Line(from, to, DebugDraw::Yellow, 2.0f);          // stays 2 s
//   DebugDraw::Sphere(p, 0.1f, DebugDraw::Cyan, 0.0f, true);     // on top of geometry
//
// LIFETIME: duration 0 = drawn in the next rendered frame only, so call it every
// frame you want it visible. duration > 0 = kept for that many seconds (always
// drawn at least once), handy for one-off events like "the hit landed here".
//
// WHERE TO CALL FROM: OnUpdate / OnRender. Calling from OnFixedUpdate works for
// timed lines, but one-frame lines will flicker, because a rendered frame can
// run zero fixed ticks.
//
// DEPTH: by default lines are depth-tested against the scene (hidden behind
// walls). onTop = true draws through everything.
//
// RENDERING: lines go into the HDR scene pass, so they get tonemapped like
// everything else. Colours above 1.0 glow (bloom). Lines are 1 px wide (wide
// lines aren't guaranteed by Vulkan).
//
// Not thread-safe: call from the main thread. This header is pure glm with no
// Vulkan, so the shape generation unit-tests directly; the renderer uploads
// and draws the result (Renderer::RecordDebugDraw).
//------------------------------------------------------------------------------
#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace Nightbloom
{
	// Same byte layout as VertexPCU (position, color, uv) so it feeds the
	// engine's shared vertex-input layout directly. uv is unused.
	struct DebugVertex
	{
		glm::vec3 position;
		glm::vec3 color;
		glm::vec2 uv{ 0.0f };
	};

	namespace DebugDraw
	{
		inline constexpr glm::vec3 White   { 1.0f, 1.0f, 1.0f };
		inline constexpr glm::vec3 Red     { 1.0f, 0.15f, 0.1f };
		inline constexpr glm::vec3 Green   { 0.2f, 1.0f, 0.25f };
		inline constexpr glm::vec3 Blue    { 0.2f, 0.45f, 1.0f };
		inline constexpr glm::vec3 Yellow  { 1.0f, 0.9f, 0.1f };
		inline constexpr glm::vec3 Cyan    { 0.1f, 0.95f, 1.0f };
		inline constexpr glm::vec3 Magenta { 1.0f, 0.2f, 0.9f };
		inline constexpr glm::vec3 Orange  { 1.0f, 0.55f, 0.05f };

		// Hard cap so a runaway loop can't eat memory; extra lines are dropped
		// (with a single warning).
		inline constexpr size_t kMaxLines = 262144;

		// Master switch. While disabled every call below is a cheap no-op and
		// nothing is drawn (existing timed lines are kept, just hidden).
		void SetEnabled(bool enabled);
		bool IsEnabled();

		//--------------------------------------------------------------------
		// Shapes. All take: color, duration (seconds, 0 = one frame), onTop.
		//--------------------------------------------------------------------
		void Line(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color,
			float duration = 0.0f, bool onTop = false);

		void Arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color,
			float headSize = 0.15f, float duration = 0.0f, bool onTop = false);

		// Three axis lines through p.
		void Cross(const glm::vec3& p, float size, const glm::vec3& color,
			float duration = 0.0f, bool onTop = false);

		// Axis-aligned box.
		void Box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color,
			float duration = 0.0f, bool onTop = false);

		// Oriented box: the local box [localMin, localMax] placed by `transform`.
		void Box(const glm::mat4& transform, const glm::vec3& localMin, const glm::vec3& localMax,
			const glm::vec3& color, float duration = 0.0f, bool onTop = false);

		void Circle(const glm::vec3& center, const glm::vec3& normal, float radius,
			const glm::vec3& color, int segments = 24, float duration = 0.0f, bool onTop = false);

		// Three great circles (XY, YZ, XZ).
		void Sphere(const glm::vec3& center, float radius, const glm::vec3& color,
			float duration = 0.0f, bool onTop = false);

		// Capsule around segment a-b: end rings, four side lines, and two
		// half-circle arcs over each cap. a == b draws a sphere.
		void Capsule(const glm::vec3& a, const glm::vec3& b, float radius, const glm::vec3& color,
			float duration = 0.0f, bool onTop = false);

		// Flat grid on the XZ plane centred on `center`, `halfCells` each way.
		void Grid(const glm::vec3& center, float cellSize, int halfCells, const glm::vec3& color,
			float duration = 0.0f, bool onTop = false);

		//--------------------------------------------------------------------
		// Renderer / housekeeping side.
		//--------------------------------------------------------------------

		// Append the current lines as a LineList (2 vertices per line).
		void BuildVertices(std::vector<DebugVertex>& out, bool onTop);

		// Called once per rendered frame after drawing: drops one-frame lines and
		// ages timed ones by `deltaTime`.
		void EndFrame(float deltaTime);

		void   Clear();
		size_t GetLineCount();
	}
}
