//------------------------------------------------------------------------------
// Vec3.hpp
//
// 3D Vector class
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------
#pragma once

#include "MathCommon.hpp"
#include <string>

namespace Nightbloom
{
	class Vec3
	{
	public: 
		float x, y, z;

		// Constructors
		Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
		Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
		explicit Vec3(float scaler) : x(scaler), y(scaler), z(scaler) {}
		//Vec3(const Vec2& vec2, float z = 0.0f) : x(vec2.x), y(vec2.y), z(z) {}

		// Basic arithmetic operations
		Vec3 operator+(const Vec3& other) const { return Vec3(x + other.x, y + other.y, z + other.z); }
		Vec3 operator-(const Vec3& other) const { return Vec3(x - other.x, y - other.y, z - other.z); }
		Vec3 operator*(float scalar) const { return Vec3(x * scalar, y * scalar, z * scalar); }
		Vec3 operator/(float scalar) const { return Vec3(x / scalar, y / scalar, z / scalar); }

		// Compound assignment operators
		Vec3& operator+=(const Vec3& other);
		Vec3& operator-=(const Vec3& other);
		Vec3& operator*=(float scalar);
		Vec3& operator/=(float scalar);
		
		// Unary operators
		Vec3 operator-() const { return Vec3(-x, -y, -z); }

		//Comparison operators
		bool operator==(const Vec3& other) const;
		bool operator!=(const Vec3& other) const { return !(*this == other); }

		// Vector operations
		float Length() const;
		float LengthSquared() const { return x * x + y * y + z * z; }
		Vec3 Normalized() const;
		void Normalize();

		float Dot(const Vec3& other) const { return x * other.x + y * other.y + z * other.z; }
		Vec3 Cross(const Vec3& other) const;

		//Static vectors
		static const Vec3 ZERO;
		static const Vec3 ONE;
		static const Vec3 UP;
		static const Vec3 DOWN;
		static const Vec3 LEFT;
		static const Vec3 RIGHT;
		static const Vec3 FORWARD;
		static const Vec3 BACKWARD;
	};

	// Non-member operators
	inline Vec3 operator*(float scalar, const Vec3& vec){ return vec * scalar; }
}