//------------------------------------------------------------------------------
// DebugDraw.cpp — see DebugDraw.hpp for the why.
//------------------------------------------------------------------------------
#include "Engine/Renderer/DebugDraw.hpp"
#include "Engine/Core/Logger/Logger.hpp"

#include <algorithm>
#include <cmath>

namespace Nightbloom
{
	namespace
	{
		struct DebugLine
		{
			glm::vec3 a;
			glm::vec3 b;
			glm::vec3 color;
			float     timeLeft;   // <= 0: draw once more, then drop
			bool      onTop;
		};

		struct DebugDrawState
		{
			std::vector<DebugLine> lines;
			bool enabled = true;
			bool warnedFull = false;
		};

		DebugDrawState& State()
		{
			static DebugDrawState s;
			return s;
		}

		constexpr float kTwoPi = 6.28318530718f;

		// Two unit vectors perpendicular to n and to each other.
		void Basis(const glm::vec3& n, glm::vec3& u, glm::vec3& v)
		{
			const glm::vec3 helper = (std::abs(n.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
			u = glm::normalize(glm::cross(helper, n));
			v = glm::cross(n, u);
		}

		// Arc from angle a0 to a1 in the plane spanned by u, v.
		void Arc(const glm::vec3& c, const glm::vec3& u, const glm::vec3& v, float r,
			float a0, float a1, int segments, const glm::vec3& color, float duration, bool onTop)
		{
			glm::vec3 prev = c + r * (std::cos(a0) * u + std::sin(a0) * v);
			for (int i = 1; i <= segments; ++i)
			{
				const float t = a0 + (a1 - a0) * (static_cast<float>(i) / segments);
				const glm::vec3 p = c + r * (std::cos(t) * u + std::sin(t) * v);
				DebugDraw::Line(prev, p, color, duration, onTop);
				prev = p;
			}
		}
	}

	namespace DebugDraw
	{
		void SetEnabled(bool enabled) { State().enabled = enabled; }
		bool IsEnabled() { return State().enabled; }

		void Line(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color, float duration, bool onTop)
		{
			DebugDrawState& s = State();
			if (!s.enabled) return;
			if (s.lines.size() >= kMaxLines)
			{
				if (!s.warnedFull)
				{
					LOG_WARN("DebugDraw: line cap ({}) reached, dropping lines. Is something drawing every tick with a long duration?", kMaxLines);
					s.warnedFull = true;
				}
				return;
			}
			s.lines.push_back({ a, b, color, duration, onTop });
		}

		void Arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color,
			float headSize, float duration, bool onTop)
		{
			if (!State().enabled) return;
			Line(from, to, color, duration, onTop);
			const glm::vec3 d = to - from;
			const float len = glm::length(d);
			if (len < 1e-6f) return;
			const glm::vec3 dir = d / len;
			glm::vec3 u, v;
			Basis(dir, u, v);
			const float h = std::min(headSize, len * 0.5f);
			const glm::vec3 base = to - dir * h;
			Line(to, base + u * h * 0.5f, color, duration, onTop);
			Line(to, base - u * h * 0.5f, color, duration, onTop);
			Line(to, base + v * h * 0.5f, color, duration, onTop);
			Line(to, base - v * h * 0.5f, color, duration, onTop);
		}

		void Cross(const glm::vec3& p, float size, const glm::vec3& color, float duration, bool onTop)
		{
			if (!State().enabled) return;
			const float h = size * 0.5f;
			Line(p - glm::vec3(h, 0, 0), p + glm::vec3(h, 0, 0), color, duration, onTop);
			Line(p - glm::vec3(0, h, 0), p + glm::vec3(0, h, 0), color, duration, onTop);
			Line(p - glm::vec3(0, 0, h), p + glm::vec3(0, 0, h), color, duration, onTop);
		}

		void Box(const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color, float duration, bool onTop)
		{
			Box(glm::mat4(1.0f), mn, mx, color, duration, onTop);
		}

		void Box(const glm::mat4& transform, const glm::vec3& mn, const glm::vec3& mx,
			const glm::vec3& color, float duration, bool onTop)
		{
			if (!State().enabled) return;
			glm::vec3 c[8];
			for (int i = 0; i < 8; ++i)
			{
				const glm::vec3 local((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
				c[i] = glm::vec3(transform * glm::vec4(local, 1.0f));
			}
			// 12 edges: pairs of corners differing in exactly one bit.
			static constexpr int kEdges[12][2] = {
				{0,1},{2,3},{4,5},{6,7},   // x
				{0,2},{1,3},{4,6},{5,7},   // y
				{0,4},{1,5},{2,6},{3,7} }; // z
			for (const auto& e : kEdges)
				Line(c[e[0]], c[e[1]], color, duration, onTop);
		}

		void Circle(const glm::vec3& center, const glm::vec3& normal, float radius,
			const glm::vec3& color, int segments, float duration, bool onTop)
		{
			if (!State().enabled) return;
			const float len = glm::length(normal);
			if (len < 1e-6f || segments < 3) return;
			glm::vec3 u, v;
			Basis(normal / len, u, v);
			Arc(center, u, v, radius, 0.0f, kTwoPi, segments, color, duration, onTop);
		}

		void Sphere(const glm::vec3& center, float radius, const glm::vec3& color, float duration, bool onTop)
		{
			if (!State().enabled) return;
			Circle(center, { 1, 0, 0 }, radius, color, 24, duration, onTop);
			Circle(center, { 0, 1, 0 }, radius, color, 24, duration, onTop);
			Circle(center, { 0, 0, 1 }, radius, color, 24, duration, onTop);
		}

		void Capsule(const glm::vec3& a, const glm::vec3& b, float radius, const glm::vec3& color,
			float duration, bool onTop)
		{
			if (!State().enabled) return;
			const glm::vec3 d = b - a;
			const float len = glm::length(d);
			if (len < 1e-6f)
			{
				Sphere(a, radius, color, duration, onTop);
				return;
			}
			const glm::vec3 axis = d / len;
			glm::vec3 u, v;
			Basis(axis, u, v);

			constexpr int kRing = 20;
			constexpr int kHalf = 10;
			constexpr float kPi = kTwoPi * 0.5f;

			// End rings.
			Arc(a, u, v, radius, 0.0f, kTwoPi, kRing, color, duration, onTop);
			Arc(b, u, v, radius, 0.0f, kTwoPi, kRing, color, duration, onTop);

			// Side lines.
			Line(a + u * radius, b + u * radius, color, duration, onTop);
			Line(a - u * radius, b - u * radius, color, duration, onTop);
			Line(a + v * radius, b + v * radius, color, duration, onTop);
			Line(a - v * radius, b - v * radius, color, duration, onTop);

			// Cap domes: half circles in the (u, axis) and (v, axis) planes.
			// At b they bulge along +axis, at a along -axis.
			Arc(b, u, axis, radius, 0.0f, kPi, kHalf, color, duration, onTop);
			Arc(b, v, axis, radius, 0.0f, kPi, kHalf, color, duration, onTop);
			Arc(a, u, -axis, radius, 0.0f, kPi, kHalf, color, duration, onTop);
			Arc(a, v, -axis, radius, 0.0f, kPi, kHalf, color, duration, onTop);
		}

		void Grid(const glm::vec3& center, float cellSize, int halfCells, const glm::vec3& color,
			float duration, bool onTop)
		{
			if (!State().enabled || halfCells <= 0 || cellSize <= 0.0f) return;
			const float ext = cellSize * halfCells;
			for (int i = -halfCells; i <= halfCells; ++i)
			{
				const float o = cellSize * i;
				Line(center + glm::vec3(o, 0, -ext), center + glm::vec3(o, 0, ext), color, duration, onTop);
				Line(center + glm::vec3(-ext, 0, o), center + glm::vec3(ext, 0, o), color, duration, onTop);
			}
		}

		void BuildVertices(std::vector<DebugVertex>& out, bool onTop)
		{
			const DebugDrawState& s = State();
			if (!s.enabled) return;
			for (const DebugLine& l : s.lines)
			{
				if (l.onTop != onTop) continue;
				out.push_back({ l.a, l.color });
				out.push_back({ l.b, l.color });
			}
		}

		void EndFrame(float deltaTime)
		{
			DebugDrawState& s = State();
			if (!s.enabled) return;   // hidden lines keep their remaining time
			auto& lines = s.lines;
			lines.erase(std::remove_if(lines.begin(), lines.end(),
				[](const DebugLine& l) { return l.timeLeft <= 0.0f; }), lines.end());
			for (DebugLine& l : lines)
				l.timeLeft -= deltaTime;
			if (lines.size() < kMaxLines) s.warnedFull = false;
		}

		void Clear()
		{
			State().lines.clear();
			State().warnedFull = false;
		}

		size_t GetLineCount() { return State().lines.size(); }
	}
}
