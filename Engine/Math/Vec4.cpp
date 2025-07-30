//------------------------------------------------------------------------------
// Vec4.cpp
//
// 4D Vector SIMD implementation
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include "Vec4.hpp"
#include <format>

namespace Nightbloom
{
	// Static vector definitions
	const Vec4 Vec4::ZERO(0.0f, 0.0f, 0.0f, 0.0f);
	const Vec4 Vec4::ONE(1.0f, 1.0f, 1.0f, 1.0f);

	Vec4& Vec4::operator+=(const Vec4& other)
	{
		m128 = _mm_add_ps(m128, other.m128);
		return *this;
	}

	Vec4& Vec4::operator-=(const Vec4& other)
	{
		m128 = _mm_sub_ps(m128, other.m128);
		return *this;
	}

	Vec4& Vec4::operator*=(float scalar)
	{
		m128 = _mm_mul_ps(m128, _mm_set1_ps(scalar));
		return *this;
	}

	Vec4& Vec4::operator/=(float scalar)
	{
		m128 = _mm_div_ps(m128, _mm_set1_ps(scalar));
		return *this;
	}

	float Vec4::Dot(const Vec4& other) const
	{
		// multiply components
		__m128 mul = _mm_mul_ps(m128, other.m128);

		//Horizontal add 
		__m128 shuf = _mm_movehdup_ps(mul); // shuffle to get xy and zw
		__m128 sums = _mm_add_ps(mul, shuf); // add xy and zw
		shuf = _mm_movehl_ps(shuf, sums); // move high to low
		sums = _mm_add_ss(sums, shuf); // add the two results

		return _mm_cvtss_f32(sums); // return the result as float
	}

	

	float Vec4::LengthSquared() const
	{
		return Dot(*this);
	}

	float Vec4::Length() const
	{
		return std::sqrt(LengthSquared());
	}

	Vec4 Vec4::Normalized() const
	{
		float len = Length();
		if (len > EPSILON)
		{
			return *this / len;
		}
		return ZERO; // Return zero vector if length is too small
	}

	void Vec4::Normalize()
	{
		float len = Length();
		if (len > EPSILON)
		{
			*this /= len; // Normalize in place
		}
		
#ifdef _DEBUG
		else
		{
			throw std::invalid_argument("Normalization of zero vector in Vec4::Normalize");
		}
#endif

	}

	std::string Vec4::ToString() const
	{
		return std::format("({:.2f}, {:.2f}, {:.2f}, {:.2f})", x, y, z, w);
	}
}
