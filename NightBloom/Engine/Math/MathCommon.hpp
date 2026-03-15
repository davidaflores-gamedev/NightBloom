//------------------------------------------------------------------------------
// MathCommon.hpp
//
// Common math definitions and constants
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#pragma once

#include <cmath>
#include <limits>
#include <immintrin.h>  // For SIMD intrinsics

namespace Nightbloom
{
	// Constants
	constexpr float PI = 3.14159265359f;
	constexpr float TWO_PI = 6.28318530718f;
	constexpr float HALF_PI = 1.57079632679f;
	constexpr float DEG_TO_RAD = PI / 180.0f;
	constexpr float RAD_TO_DEG = 180.0f / PI;
	constexpr float EPSILON = std::numeric_limits<float>::epsilon();

#define NIGHTBLOOM_ALIGN16 __declspec(align(16))

	// Utility functions
	inline bool IsZero(float value)
	{
		return std::fabs(value) < EPSILON;
	}

	inline bool IsEqual(float a, float b)
	{
		return std::fabs(a - b) < EPSILON;
	}
}