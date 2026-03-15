//------------------------------------------------------------------------------
// MathTests.cpp
//
// Unit tests for math library
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include <gtest/gtest.h>
#include "../Math/Vec2.hpp"
#include "../Math/Vec3.hpp"
#include "../Math/Vec4.hpp"
#include "../Math/MathUtils.hpp"

using namespace Nightbloom;

TEST(SanityCheck, BasicMath)
{
	EXPECT_EQ(2 + 2, 4);
	EXPECT_TRUE(true);
	EXPECT_FALSE(false);
}

// Test that will be useful later
TEST(SanityCheck, FloatComparison)
{
	float a = 0.1f + 0.2f;
	EXPECT_NEAR(a, 0.3f, 0.0001f);  // Float comparison with tolerance
}

// Vec2 Tests
TEST(Vec2Test, Construction)
{
	Vec2 v1;
	EXPECT_FLOAT_EQ(v1.x, 0.0f);
	EXPECT_FLOAT_EQ(v1.y, 0.0f);

	Vec2 v2(3.0f, 4.0f);
	EXPECT_FLOAT_EQ(v2.x, 3.0f);
	EXPECT_FLOAT_EQ(v2.y, 4.0f);
}

TEST(Vec2Test, Addition)
{
	Vec2 a(1.0f, 2.0f);
	Vec2 b(3.0f, 4.0f);
	Vec2 c = a + b;

	EXPECT_FLOAT_EQ(c.x, 4.0f);
	EXPECT_FLOAT_EQ(c.y, 6.0f);
}

TEST(Vec2Test, DotProduct)
{
	Vec2 a(3.0f, 4.0f);
	Vec2 b(2.0f, 1.0f);
	float dot = a.Dot(b);

	EXPECT_FLOAT_EQ(dot, 10.0f); // 3*2 + 4*1 = 10
}

TEST(Vec2Test, Length)
{
	Vec2 v(3.0f, 4.0f);
	EXPECT_FLOAT_EQ(v.Length(), 5.0f); // 3-4-5 triangle
}

// Vec4 SIMD Tests
TEST(Vec4Test, SIMDAddition)
{
	Vec4 a(1.0f, 2.0f, 3.0f, 4.0f);
	Vec4 b(5.0f, 6.0f, 7.0f, 8.0f);
	Vec4 c = a + b;

	EXPECT_FLOAT_EQ(c.x, 6.0f);
	EXPECT_FLOAT_EQ(c.y, 8.0f);
	EXPECT_FLOAT_EQ(c.z, 10.0f);
	EXPECT_FLOAT_EQ(c.w, 12.0f);
}

TEST(Vec4Test, SIMDDotProduct)
{
	Vec4 a(2.0f, 3.0f, 4.0f, 5.0f);
	Vec4 b(1.0f, 2.0f, 3.0f, 4.0f);
	float dot = a.Dot(b);

	EXPECT_FLOAT_EQ(dot, 40.0f); // 2*1 + 3*2 + 4*3 + 5*4 = 40
}

// Math Utils Tests
TEST(MathUtilsTest, Clamp)
{
	EXPECT_FLOAT_EQ(Clamp(5.0f, 0.0f, 10.0f), 5.0f);
	EXPECT_FLOAT_EQ(Clamp(-5.0f, 0.0f, 10.0f), 0.0f);
	EXPECT_FLOAT_EQ(Clamp(15.0f, 0.0f, 10.0f), 10.0f);
}

TEST(MathUtilsTest, Lerp)
{
	EXPECT_FLOAT_EQ(Lerp(0.0f, 10.0f, 0.5f), 5.0f);
	EXPECT_FLOAT_EQ(Lerp(0.0f, 10.0f, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(Lerp(0.0f, 10.0f, 1.0f), 10.0f);
}