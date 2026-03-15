//------------------------------------------------------------------------------
// Vec4.hpp
//
// 4D Vector class with SIMD optimization
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------
#pragma once

#include "MathCommon.hpp"
#include <string>

namespace Nightbloom
{
	class NIGHTBLOOM_ALIGN16 Vec4
	{
	public:
		union
		{
			struct { float x, y, z, w; };
			__m128 m128; // SIMD vector for performance
		};
	

	//Constructors
	Vec4() : m128(_mm_setzero_ps()) {}
	Vec4(float x, float y, float z, float w) : m128(_mm_set_ps(w, z, y, x)) {}
	explicit Vec4(float scalar) : m128(_mm_set1_ps(scalar)) {}
	explicit Vec4(__m128 vec) : m128(vec) {}

	// SIMD arithmetic operations
	Vec4 operator+(const Vec4& other) const { return Vec4(_mm_add_ps(m128, other.m128)); }
	Vec4 operator-(const Vec4& other) const { return Vec4(_mm_sub_ps(m128, other.m128)); }
	Vec4 operator*(const Vec4& other) const { return Vec4(_mm_mul_ps(m128, other.m128)); }
	Vec4 operator/(const Vec4& other) const { return Vec4(_mm_div_ps(m128, other.m128)); }

	Vec4 operator*(float scalar) const { return Vec4(_mm_mul_ps(m128, _mm_set1_ps(scalar))); }
	Vec4 operator/(float scalar) const { return Vec4(_mm_div_ps(m128, _mm_set1_ps(scalar))); }

	//Compound assignment operators
	Vec4& operator+=(const Vec4& other);
	Vec4& operator-=(const Vec4& other);
	Vec4& operator*=(float scalar);
	Vec4& operator/=(float scalar);

	//Unary operators
	Vec4 operator-() const { return Vec4(_mm_sub_ps(_mm_setzero_ps(), m128)); }

	//Vector operations (SIMD optimized)

	float Length() const;
	float LengthSquared() const;
	Vec4 Normalized() const;
	void Normalize();

	float Dot(const Vec4& other) const;
	//Vec4 Cross(const Vec4& other) const;

	//Component access
	float operator[](int index) const {return (&x)[index]; }
	float& operator[](int index) { return (&x)[index]; }

	//String representation
	std::string ToString() const;

	//Static utility functions
	static const Vec4 ZERO;
	static const Vec4 ONE;
	static const Vec4 UP;
	static const Vec4 DOWN;
	static const Vec4 NORTH;
	static const Vec4 EAST;
	static const Vec4 SOUTH;
	static const Vec4 WEST;
	};

	// Non-member operators
	inline Vec4 operator*(float scalar, const Vec4& vec){ return vec * scalar; }
}