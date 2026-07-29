//------------------------------------------------------------------------------
// MathUtils.hpp
//
// Common math utility functions
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------
#pragma once

#include "MathCommon.hpp"

namespace Nightbloom
{
	//clamping
	template <typename T>
	inline T Clamp(T value, T min, T max)
	{
		return value < min ? min : (value > max ? max : value);
	}

	// Lerp function
	template <typename T>
	inline T Lerp(T a, T b, float t)
	{
		return a + (b - a) * t;
	}

	// Smoothstep function
	template <typename T>
	inline T SmoothStep(T edge0, T edge1, float x)
	{
		float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// Angle conversion functions
	inline float DegreesToRadians(float degrees) { return degrees * DEG_TO_RAD; }
	inline float RadiansToDegrees(float radians) { return radians * RAD_TO_DEG; }

	// Shortest signed angular difference in DEGREES, wrapped to [-180, 180].
	// Used by everything that slews a yaw toward a target (character facing,
	// guard turning). Without the wrap, turning across the 0/360 seam takes the
	// long way round — a 10-degree turn becomes a 350-degree spin.
	inline float ShortestAngleDelta(float fromDegrees, float toDegrees)
	{
		return std::fmod(toDegrees - fromDegrees + 540.0f, 360.0f) - 180.0f;
	}

	// Float Comparison functions
	inline bool IsNearlyEqual(float a, float b, float epsilon = EPSILON)
	{
		return std::fabs(a - b) < epsilon;
	}

	// Fast inverse square root (Quake's method)
	inline float FastInvSqrt(float x)
	{
		float xhalf = 0.5f * x;
		int i = *(int*)&x; // Bit-level manipulation
		i = 0x5f3759df - (i >> 1); // Magic number
		x = *(float*)&i; // Convert back to float
		x = x * (1.5f - xhalf * x * x); // Newton's method for refinement
		return x;
	}
}