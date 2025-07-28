#include <gtest/gtest.h>

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