//------------------------------------------------------------------------------
// DebugDrawTests.cpp
//
// CPU side of DebugDraw (Engine/Renderer/DebugDraw.hpp): shape generation,
// lifetime, depth/overlay split. The GPU upload/draw is covered by running the
// editor; this pins the rules games rely on.
//------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "Engine/Renderer/DebugDraw.hpp"

#include <algorithm>

using namespace Nightbloom;

namespace
{
	class DebugDrawTest : public ::testing::Test
	{
	protected:
		void SetUp() override { DebugDraw::SetEnabled(true); DebugDraw::Clear(); }
		void TearDown() override { DebugDraw::SetEnabled(true); DebugDraw::Clear(); }

		static size_t Verts(bool onTop)
		{
			std::vector<DebugVertex> v;
			DebugDraw::BuildVertices(v, onTop);
			return v.size();
		}
	};
}

TEST_F(DebugDrawTest, VertexMatchesSharedLayoutSize)
{
	// Must stay byte-identical to VertexPCU (vec3 + vec3 + vec2).
	EXPECT_EQ(sizeof(DebugVertex), 32u);
}

TEST_F(DebugDrawTest, LineProducesTwoVerticesWithColor)
{
	DebugDraw::Line({ 0, 0, 0 }, { 1, 2, 3 }, DebugDraw::Red);
	std::vector<DebugVertex> v;
	DebugDraw::BuildVertices(v, false);
	ASSERT_EQ(v.size(), 2u);
	EXPECT_FLOAT_EQ(v[1].position.z, 3.0f);
	EXPECT_FLOAT_EQ(v[0].color.r, DebugDraw::Red.r);
}

TEST_F(DebugDrawTest, ShapeLineCounts)
{
	DebugDraw::Box({ -1, -1, -1 }, { 1, 1, 1 }, DebugDraw::White);
	EXPECT_EQ(DebugDraw::GetLineCount(), 12u);
	DebugDraw::Clear();

	DebugDraw::Cross({ 0, 0, 0 }, 1.0f, DebugDraw::White);
	EXPECT_EQ(DebugDraw::GetLineCount(), 3u);
	DebugDraw::Clear();

	DebugDraw::Arrow({ 0, 0, 0 }, { 0, 0, 2 }, DebugDraw::White);
	EXPECT_EQ(DebugDraw::GetLineCount(), 5u);   // shaft + 4 head lines
	DebugDraw::Clear();

	DebugDraw::Circle({ 0, 0, 0 }, { 0, 1, 0 }, 1.0f, DebugDraw::White, 16);
	EXPECT_EQ(DebugDraw::GetLineCount(), 16u);
	DebugDraw::Clear();

	DebugDraw::Sphere({ 0, 0, 0 }, 1.0f, DebugDraw::White);
	EXPECT_EQ(DebugDraw::GetLineCount(), 3u * 24u);
	DebugDraw::Clear();

	DebugDraw::Grid({ 0, 0, 0 }, 1.0f, 2, DebugDraw::White);
	EXPECT_EQ(DebugDraw::GetLineCount(), 2u * 5u);
}

TEST_F(DebugDrawTest, CircleVerticesLieOnTheCircle)
{
	DebugDraw::Circle({ 1, 2, 3 }, { 0, 0, 1 }, 0.5f, DebugDraw::White, 12);
	std::vector<DebugVertex> v;
	DebugDraw::BuildVertices(v, false);
	for (const DebugVertex& p : v)
	{
		EXPECT_NEAR(glm::length(p.position - glm::vec3(1, 2, 3)), 0.5f, 1e-4f);
		EXPECT_NEAR(p.position.z, 3.0f, 1e-4f);   // in the plane facing +Z
	}
}

TEST_F(DebugDrawTest, CapsuleStaysWithinItsRadius)
{
	const glm::vec3 a(0, 0.3f, 0), b(0, 1.5f, 0);
	DebugDraw::Capsule(a, b, 0.3f, DebugDraw::Green);
	std::vector<DebugVertex> v;
	DebugDraw::BuildVertices(v, false);
	ASSERT_FALSE(v.empty());
	float maxY = -1e9f, minY = 1e9f;
	for (const DebugVertex& p : v)
	{
		// Every vertex is exactly `radius` from the core segment.
		const float t = std::clamp(p.position.y, a.y, b.y);
		EXPECT_NEAR(glm::length(p.position - glm::vec3(0, t, 0)), 0.3f, 1e-4f);
		maxY = std::max(maxY, p.position.y);
		minY = std::min(minY, p.position.y);
	}
	EXPECT_NEAR(maxY, 1.8f, 1e-3f);   // dome reaches the top
	EXPECT_NEAR(minY, 0.0f, 1e-3f);   // and the bottom
}

TEST_F(DebugDrawTest, DegenerateCapsuleIsASphere)
{
	DebugDraw::Capsule({ 0, 0, 0 }, { 0, 0, 0 }, 1.0f, DebugDraw::White);
	EXPECT_EQ(DebugDraw::GetLineCount(), 3u * 24u);
}

TEST_F(DebugDrawTest, OneFrameLinesLastOneFrame)
{
	DebugDraw::Line({ 0, 0, 0 }, { 1, 0, 0 }, DebugDraw::White);
	EXPECT_EQ(Verts(false), 2u);
	DebugDraw::EndFrame(0.016f);
	EXPECT_EQ(DebugDraw::GetLineCount(), 0u);
}

TEST_F(DebugDrawTest, TimedLinesExpireAfterDuration)
{
	DebugDraw::Line({ 0, 0, 0 }, { 1, 0, 0 }, DebugDraw::White, 0.05f);
	DebugDraw::EndFrame(0.016f);  // 0.034 left
	DebugDraw::EndFrame(0.016f);  // 0.018 left
	DebugDraw::EndFrame(0.016f);  // 0.002 left
	EXPECT_EQ(DebugDraw::GetLineCount(), 1u);
	DebugDraw::EndFrame(0.016f);  // goes <= 0: drawn this last frame
	EXPECT_EQ(DebugDraw::GetLineCount(), 1u);
	DebugDraw::EndFrame(0.016f);  // dropped
	EXPECT_EQ(DebugDraw::GetLineCount(), 0u);
}

TEST_F(DebugDrawTest, TinyDurationStillDrawnOnce)
{
	// Duration shorter than a frame must not vanish before it is ever drawn.
	DebugDraw::Line({ 0, 0, 0 }, { 1, 0, 0 }, DebugDraw::White, 0.001f);
	DebugDraw::EndFrame(0.016f);
	EXPECT_EQ(DebugDraw::GetLineCount(), 1u);
	DebugDraw::EndFrame(0.016f);
	EXPECT_EQ(DebugDraw::GetLineCount(), 0u);
}

TEST_F(DebugDrawTest, OverlayLinesAreSeparated)
{
	DebugDraw::Line({ 0, 0, 0 }, { 1, 0, 0 }, DebugDraw::White);
	DebugDraw::Box({ 0, 0, 0 }, { 1, 1, 1 }, DebugDraw::Red, 0.0f, /*onTop*/ true);
	EXPECT_EQ(Verts(false), 2u);
	EXPECT_EQ(Verts(true), 24u);
}

TEST_F(DebugDrawTest, DisabledIsANoOpAndHidesExisting)
{
	DebugDraw::Line({ 0, 0, 0 }, { 1, 0, 0 }, DebugDraw::White, 5.0f);
	DebugDraw::SetEnabled(false);
	DebugDraw::Box({ 0, 0, 0 }, { 1, 1, 1 }, DebugDraw::White);
	EXPECT_EQ(DebugDraw::GetLineCount(), 1u);   // the box was ignored
	EXPECT_EQ(Verts(false), 0u);                // and nothing is drawn
	DebugDraw::EndFrame(10.0f);                 // timed line doesn't age while hidden
	DebugDraw::SetEnabled(true);
	EXPECT_EQ(Verts(false), 2u);
}

TEST_F(DebugDrawTest, LineCapDropsExtras)
{
	for (size_t i = 0; i < DebugDraw::kMaxLines + 10; ++i)
		DebugDraw::Line({ 0, 0, 0 }, { 1, 0, 0 }, DebugDraw::White);
	EXPECT_EQ(DebugDraw::GetLineCount(), DebugDraw::kMaxLines);
}
