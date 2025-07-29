//------------------------------------------------------------------------------
// Vec2.cpp
//
// 2D Vector implementation
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "Vec2.hpp"
#include <format>

namespace Nightbloom
{
	// Static vector definitions
	const Vec2 Vec2::ZERO(0.0f, 0.0f);
	const Vec2 Vec2::ONE(1.0f, 1.0f);
	const Vec2 Vec2::UP(0.0f, 1.0f);
	const Vec2 Vec2::DOWN(0.0f, -1.0f);
	const Vec2 Vec2::LEFT(-1.0f, 0.0f);
	const Vec2 Vec2::RIGHT(1.0f, 0.0f);
	
	Vec2& Vec2::operator+=(const Vec2& other)
	{
		x += other.x;
		y += other.y;
		return *this;
	}

	Vec2& Vec2::operator-=(const Vec2& other)
	{
		x -= other.x;
		y -= other.y;
		return *this;
	}

	Vec2& Vec2::operator*=(float scalar)
	{
		x *= scalar;
		y *= scalar;
		return *this;
	}

	Vec2& Vec2::operator/=(float scalar)
	{
#ifdef _DEBUG
		{
			if (IsZero(scalar))
			{
				throw std::invalid_argument("Division by zero in Vec2 operator/=");
			}
		}
#endif
		x /= scalar;
		y /= scalar;
		return *this;
	}

	bool Vec2::operator==(const Vec2& other) const
	{
		return std::abs(x - other.x) < EPSILON && 
			   std::abs(y - other.y) < EPSILON;
		// try sometime return IsEqual(x, other.x) && IsEqual(y, other.y);
	}

	float Vec2::Length() const
	{
		return std::sqrt(LengthSquared());
	}

	Vec2 Vec2::Normalized() const
	{
		float len = Length();
		if (len > EPSILON)
		{
			return *this / len; // Return zero vector if length is too small
		}
		return Vec2::ZERO; // Avoid division by zero
	}

	void Vec2::Normalize()
	{
		float len = Length();
		if (len > EPSILON)
		{
			*this /= len;
		}
	}

	std::string Vec2::ToString() const
	{
		return std::format("({:.2f}, {:.2f})", x, y);
	}
}