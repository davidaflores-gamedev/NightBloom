#pragma once

//------------------------------------------------------------------------------
// CameraPath.hpp
//
// Keyframed camera path for scripted shots (editor demo mode, attract loops).
// Position and yaw/pitch are interpolated with a uniform Catmull-Rom spline, so
// the camera passes THROUGH every key with continuous velocity — no visible
// "corners" at keys, which is what makes a recorded fly-through look deliberate.
//
// Angles are degrees in the same convention as Camera::SetRotation (yaw around
// +Y, pitch up from the horizon). Keys may give an explicit yaw/pitch or a
// look-at point; yaw is unwrapped between keys so it always turns the short way.
//------------------------------------------------------------------------------

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Nightbloom
{
	struct CameraKey
	{
		float     time = 0.0f;   // seconds from the start of the path
		glm::vec3 position = glm::vec3(0.0f);
		float     yaw = 0.0f;    // degrees
		float     pitch = 0.0f;  // degrees
	};

	struct CameraPose
	{
		glm::vec3 position = glm::vec3(0.0f);
		float     yaw = 0.0f;
		float     pitch = 0.0f;
	};

	class CameraPath
	{
	public:
		// Yaw/pitch that make a camera at `from` look at `target`.
		static void LookAtAngles(const glm::vec3& from, const glm::vec3& target, float& yaw, float& pitch)
		{
			glm::vec3 d = target - from;
			const float len = glm::length(d);
			if (len < 1e-5f) { yaw = 0.0f; pitch = 0.0f; return; }
			d /= len;
			yaw = glm::degrees(std::atan2(d.z, d.x));
			pitch = glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f)));
		}

		void Clear() { m_Keys.clear(); }

		// Keys must be added in increasing time order.
		void AddKey(CameraKey key)
		{
			// Unwrap yaw against the previous key so interpolation takes the short way.
			if (!m_Keys.empty())
			{
				const float prev = m_Keys.back().yaw;
				while (key.yaw - prev > 180.0f) key.yaw -= 360.0f;
				while (key.yaw - prev < -180.0f) key.yaw += 360.0f;
			}
			m_Keys.push_back(key);
		}

		bool  Empty() const { return m_Keys.empty(); }
		float Duration() const { return m_Keys.empty() ? 0.0f : m_Keys.back().time; }

		CameraPose Evaluate(float t) const
		{
			CameraPose pose;
			if (m_Keys.empty()) return pose;
			if (m_Keys.size() == 1 || t <= m_Keys.front().time) return ToPose(m_Keys.front());
			if (t >= m_Keys.back().time) return ToPose(m_Keys.back());

			size_t i = 0;
			while (i + 1 < m_Keys.size() && m_Keys[i + 1].time < t) ++i;

			const CameraKey& k1 = m_Keys[i];
			const CameraKey& k2 = m_Keys[i + 1];
			const CameraKey& k0 = (i > 0) ? m_Keys[i - 1] : k1;
			const CameraKey& k3 = (i + 2 < m_Keys.size()) ? m_Keys[i + 2] : k2;

			const float span = std::max(1e-4f, k2.time - k1.time);
			const float u = (t - k1.time) / span;

			pose.position = CatmullRom(k0.position, k1.position, k2.position, k3.position, u);
			pose.yaw      = CatmullRom(k0.yaw,      k1.yaw,      k2.yaw,      k3.yaw,      u);
			pose.pitch    = CatmullRom(k0.pitch,    k1.pitch,    k2.pitch,    k3.pitch,    u);
			pose.pitch    = std::clamp(pose.pitch, -89.0f, 89.0f);
			return pose;
		}

	private:
		std::vector<CameraKey> m_Keys;

		static CameraPose ToPose(const CameraKey& k) { return { k.position, k.yaw, k.pitch }; }

		template <typename T>
		static T CatmullRom(const T& p0, const T& p1, const T& p2, const T& p3, float u)
		{
			const float u2 = u * u, u3 = u2 * u;
			return 0.5f * ((2.0f * p1) + (-p0 + p2) * u
				+ (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2
				+ (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u3);
		}
	};

} // namespace Nightbloom
