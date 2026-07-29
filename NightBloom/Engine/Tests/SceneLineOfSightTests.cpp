//------------------------------------------------------------------------------
// SceneLineOfSightTests.cpp
//
// Scene-level line-of-sight tests — the layer where perception meets actual
// scene objects.
//
// WHY THIS FILE EXISTS: RaycastTests covers the geometry and passed fully green
// while line-of-sight was completely broken in game, because the bug wasn't in
// the math. Cover objects were added without a PrimitiveKind, so
// SceneObject::GetLocalBounds refused them and Scene::Raycast skipped every one
// — geometry that rendered perfectly and blocked nothing. Testing the primitives
// in isolation can never catch that. These tests go through Scene.
//------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "Engine/Core/Scene.hpp"
#include <glm/gtc/matrix_transform.hpp>

using namespace Nightbloom;

namespace
{
	// Adds an untextured cube primitive of the given size at the given position.
	// meshDrawable is null (no GPU here), so GetLocalBounds falls back to
	// primitiveTransform — which is what we set.
	SceneObject* AddCube(Scene& scene, const char* name,
		const glm::vec3& position, const glm::vec3& scale)
	{
		SceneObject* obj = scene.AddPrimitive(name, nullptr, PrimitiveKind::TestCube);
		obj->primitiveTransform =
			glm::translate(glm::mat4(1.0f), position) *
			glm::scale(glm::mat4(1.0f), scale);
		return obj;
	}
}

TEST(SceneLineOfSight, ClearPathIsVisible)
{
	Scene scene;
	EXPECT_TRUE(scene.HasLineOfSight(glm::vec3(-10, 1, 0), glm::vec3(10, 1, 0)));
}

// The regression that started this file: a cube between two points must block.
TEST(SceneLineOfSight, CubeBetweenPointsBlocksSight)
{
	Scene scene;
	AddCube(scene, "Cover", glm::vec3(0, 1, 0), glm::vec3(2.0f, 4.0f, 2.0f));

	EXPECT_FALSE(scene.HasLineOfSight(glm::vec3(-10, 1, 0), glm::vec3(10, 1, 0)))
		<< "a cube spanning the sight line must block it";
}

// The exact failure mode: geometry present and rendering, but with no kind tag,
// so it silently has no bounds and blocks nothing.
TEST(SceneLineOfSight, PrimitiveWithoutKindDoesNotBlockAndIsDetectable)
{
	Scene scene;
	SceneObject* obj = scene.AddPrimitive("Untagged", nullptr);   // kind defaults to None
	obj->primitiveTransform =
		glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)) *
		glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 4.0f, 2.0f));

	glm::vec3 mn, mx; glm::mat4 xf;
	EXPECT_FALSE(obj->GetLocalBounds(mn, mx, xf))
		<< "an untagged primitive has no known bounds — this is why it cannot block";

	EXPECT_TRUE(scene.HasLineOfSight(glm::vec3(-10, 1, 0), glm::vec3(10, 1, 0)))
		<< "documents the trap: untagged geometry is invisible to raycasts";
}

TEST(SceneLineOfSight, CubeOffToTheSideDoesNotBlock)
{
	Scene scene;
	AddCube(scene, "Cover", glm::vec3(0, 1, 20), glm::vec3(2.0f, 4.0f, 2.0f));

	EXPECT_TRUE(scene.HasLineOfSight(glm::vec3(-10, 1, 0), glm::vec3(10, 1, 0)));
}

// Peeking over cover has to work, or there's no crouch/stand gameplay later.
TEST(SceneLineOfSight, SightPassesOverAShortWall)
{
	Scene scene;
	AddCube(scene, "LowWall", glm::vec3(0, 0.5f, 0), glm::vec3(4.0f, 1.0f, 4.0f));

	EXPECT_FALSE(scene.HasLineOfSight(glm::vec3(-10, 0.5f, 0), glm::vec3(10, 0.5f, 0)))
		<< "at wall height the wall blocks";
	EXPECT_TRUE(scene.HasLineOfSight(glm::vec3(-10, 5.0f, 0), glm::vec3(10, 5.0f, 0)))
		<< "well above the wall the line is clear";
}

// Actors must not occlude rays aimed at themselves, or a guard can never see
// anyone (its target's own body box stops the ray first).
TEST(SceneLineOfSight, BlocksSightFlagIsRespected)
{
	Scene scene;
	SceneObject* actor = AddCube(scene, "Actor", glm::vec3(0, 1, 0), glm::vec3(2.0f, 4.0f, 2.0f));

	ASSERT_FALSE(scene.HasLineOfSight(glm::vec3(-10, 1, 0), glm::vec3(10, 1, 0)));

	actor->blocksSight = false;
	EXPECT_TRUE(scene.HasLineOfSight(glm::vec3(-10, 1, 0), glm::vec3(10, 1, 0)));
}

TEST(SceneLineOfSight, HiddenObjectsDoNotBlock)
{
	Scene scene;
	SceneObject* obj = AddCube(scene, "Cover", glm::vec3(0, 1, 0), glm::vec3(2.0f, 4.0f, 2.0f));

	ASSERT_FALSE(scene.HasLineOfSight(glm::vec3(-10, 1, 0), glm::vec3(10, 1, 0)));

	obj->visible = false;
	EXPECT_TRUE(scene.HasLineOfSight(glm::vec3(-10, 1, 0), glm::vec3(10, 1, 0)));
}

// Terrain occlusion runs through the same entry point.
TEST(SceneLineOfSight, TerrainRidgeBlocks)
{
	Scene scene;
	auto ridge = [](float x, float) { return 10.0f - std::fabs(x) * 0.5f; };

	EXPECT_FALSE(scene.HasLineOfSight(glm::vec3(-20, 1, 0), glm::vec3(20, 1, 0), ridge))
		<< "a hill between the points must block";
	EXPECT_TRUE(scene.HasLineOfSight(glm::vec3(-20, 40, 0), glm::vec3(20, 40, 0), ridge))
		<< "above the hill the line is clear";
}

// Rotation must be handled exactly: a long thin wall rotated 90 degrees no
// longer spans the sight line, and a fattened world AABB would wrongly block.
TEST(SceneLineOfSight, RotationIsHandledExactly)
{
	Scene scene;
	SceneObject* wall = scene.AddPrimitive("Wall", nullptr, PrimitiveKind::TestCube);

	// Thin in X, long in Z: spans across a ray travelling down +X.
	wall->primitiveTransform =
		glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)) *
		glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 4.0f, 12.0f));
	EXPECT_FALSE(scene.HasLineOfSight(glm::vec3(-10, 1, 0), glm::vec3(10, 1, 0)));

	// Rotated 90 degrees about Y it lies ALONG the ray instead, still crossing
	// the origin, so it continues to block — but now by its short axis.
	wall->primitiveTransform =
		glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 30.0f)) *
		glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0)) *
		glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 4.0f, 12.0f));
	EXPECT_TRUE(scene.HasLineOfSight(glm::vec3(-10, 1, 0), glm::vec3(10, 1, 0)))
		<< "moved well off the sight line, it must not block";
}
