//------------------------------------------------------------------------------
// Vec2.hpp
//
// 2D Vector class
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------
#pragma once

#include "MathCommon.hpp"
#include <string>

namespace Nightbloom
{
	class Vec2
	{
	public:
		float x, y;

		Vec2() : x(0.0f), y(0.0f) {}
		Vec2(float x, float y) : x(x), y(y) {}

		// Basic arithmetic operations
		Vec2 operator+(const Vec2& other) const { return Vec2(x + other.x, y + other.y); }
		Vec2 operator-(const Vec2& other) const { return Vec2(x - other.x, y - other.y); }
		Vec2 operator*(float scalar) const { return Vec2(x * scalar, y * scalar); }
		Vec2 operator/(float scalar) const { return Vec2(x / scalar, y / scalar); }

		// Compound assignment operators
		Vec2& operator+=(const Vec2& other); //{ x += other.x; y += other.y; return *this; }
		Vec2& operator-=(const Vec2& other); //{ x -= other.x; y -= other.y; return *this; }
		Vec2& operator*=(float scalar); //{ x *= scalar; y *= scalar; return *this; }
		Vec2& operator/=(float scalar); //{ x /= scalar; y /= scalar; return *this; }

		// Unary operators
		Vec2 operator-() const { return Vec2(-x, -y); }

		// Comparison operators
		bool operator==(const Vec2& other) const; //{ return IsEqual(x, other.x) && IsEqual(y, other.y); }
		bool operator!=(const Vec2& other) const { return !(*this == other); }

		// Vector operations
		float Length() const;
		float LengthSquared() const { return x * x + y * y; };
		Vec2 Normalized() const;
		void Normalize();

		float Dot(const Vec2& other) const { return x * other.x + y * other.y; }
		float Cross(const Vec2& other) const { return x * other.y - y * other.x; }

		// String representation
		std::string ToString() const;

		// Static utility functions
		static const Vec2 ZERO;
		static const Vec2 ONE;
		static const Vec2 UP;
		static const Vec2 DOWN;
		static const Vec2 LEFT;
		static const Vec2 RIGHT;
	};

	// Non-member operators
	inline Vec2 operator*(float scalar, const Vec2& vec)
	{
		return vec * scalar;
	}
}