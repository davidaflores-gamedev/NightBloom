//------------------------------------------------------------------------------
// SceneSerializer.hpp
//
// Saves/loads a Scene (objects, lights, ambient) plus a camera pose to/from a
// JSON file. This is the persistence layer that makes "projects" possible — see
// .claude/PACKAGING_ROADMAP.md (Milestone 0). Format is versioned so it can
// evolve without breaking older files.
//
// Load reconstructs GPU-backed content (GLTF models, primitive geometry) through
// the Renderer, so a valid, initialized Renderer must be passed to Load().
//------------------------------------------------------------------------------
#pragma once

#include <glm/glm.hpp>
#include <string>

namespace Nightbloom
{
	class Scene;
	class Renderer;

	// Minimal camera pose carried alongside the scene. Kept as a POD (rather than
	// the Camera class) so the engine's serializer doesn't depend on the free-fly
	// Camera implementation — the editor maps its Camera to/from this.
	struct SceneCameraState
	{
		glm::vec3 position = glm::vec3(0.0f);
		float yaw = -90.0f;
		float pitch = 0.0f;
		float fov = 45.0f;        // vertical FOV, degrees
		float nearPlane = 0.1f;   // infinite-far reverse-Z, so no far plane stored
	};

	class SceneSerializer
	{
	public:
		// Serialize scene + camera to a JSON file. 'editorStateJson', if non-empty,
		// must be a JSON object string; it's stored verbatim under the top-level
		// "editor" key. This lets the editor persist panel state (e.g. the day/night
		// cycle) without the engine serializer knowing anything about editor panels.
		// Returns false on IO error.
		static bool Save(const Scene& scene, const SceneCameraState& camera,
			const std::string& filepath, const std::string& sceneName = "Untitled",
			const std::string& editorStateJson = std::string());

		// Load a JSON scene file. Clears 'scene' (objects AND lights), then rebuilds
		// it, reconstructing models/primitives via 'renderer'. Fills 'camera'. If
		// 'outEditorStateJson' is non-null, receives the "editor" object as a JSON
		// string ("{}" if absent). Returns false on IO/parse error.
		static bool Load(Scene& scene, SceneCameraState& camera,
			const std::string& filepath, Renderer* renderer,
			std::string* outEditorStateJson = nullptr);
	};

} // namespace Nightbloom
