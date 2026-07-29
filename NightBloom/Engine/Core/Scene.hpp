//------------------------------------------------------------------------------
// Scene.hpp
//
// Minimal scene management - SceneObject wrapper and Scene container
// MVP implementation for editor hierarchy/inspector
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/Model.hpp"
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include "Engine/Renderer/Light.hpp"
#include "Engine/Renderer/Frustum.hpp"
#include "Engine/Physics/Raycast.hpp"
#include "Engine/Physics/Collision.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <memory>

namespace Nightbloom
{
	//--------------------------------------------------------------------------
	// Primitive geometry kinds. Primitives (MeshDrawable) reference engine-owned
	// built-in vertex/index buffers that have no file path, so serialization needs
	// a tag to know which built-in geometry to rebind on load. None = not a
	// primitive (either a model object, or geometry we can't yet reconstruct).
	//--------------------------------------------------------------------------
	enum class PrimitiveKind
	{
		None = 0,
		TestCube,
		GroundPlane,
		MoonSphere
	};

	//--------------------------------------------------------------------------
	// SceneObject - Wrapper for anything that can exist in the scene
	//--------------------------------------------------------------------------
	struct SceneObject
	{
		std::string name;
		bool visible = true;

		// Whether this object occludes line-of-sight queries (Scene::Raycast).
		// Separate from `visible` on purpose: a character's own body is very
		// much visible but must not block sight rays *to that character*, or a
		// guard can never see anyone. Clear it on actors; leave it set on world
		// geometry. (An invisible collision volume would be the mirror case —
		// visible=false, blocksSight=true.)
		bool blocksSight = true;

		// Whether this object stops a character walking through it. Separate
		// from blocksSight because the two genuinely differ: a bush blocks
		// sight but not movement, a glass pane blocks movement but not sight.
		bool blocksMovement = true;

		// Serialization metadata for primitives (meshDrawable-based objects). The
		// transform + customData live in the MeshDrawable itself (read via its
		// getters); these record what the drawable can't: which built-in geometry
		// and which named texture to rebind on load.
		PrimitiveKind primitiveKind = PrimitiveKind::None;
		std::string   primitiveTexture;   // ResourceManager texture name, e.g. "uv_checker"

		// The actual renderable (we own the model, drawable is created from it)
		std::unique_ptr<Model> model;
		std::unique_ptr<ModelDrawable> drawable;

		// For obhects without a model
		std::unique_ptr<MeshDrawable> meshDrawable;

		// Track primitive state for UI sync
		int textureIndex = 0;        // Which texture is assigned (for UI display)
		PipelineType pipeline = PipelineType::Mesh;  // Which pipeline (for primitives)

		// Composed primitive transform (cache). Authoritative source is the
		// decomposed TRS below; DayNight also writes this directly for the moon.
		glm::mat4 primitiveTransform = glm::mat4(1.0f);

		// Decomposed transform for primitives (meshDrawable has no TRS of its own,
		// unlike Model). The accessors below edit these and recompose into the
		// drawable, so the Inspector can move/rotate/scale primitives exactly like
		// models. Rotation is euler radians, matching Model.
		glm::vec3 primitivePosition = glm::vec3(0.0f);
		glm::vec3 primitiveRotation = glm::vec3(0.0f);
		glm::vec3 primitiveScale    = glm::vec3(1.0f);

		// Recompose primitiveTransform from the TRS and push it to the drawable.
		// Same order as Model::Set* (T * R * S) so primitives and models behave
		// identically in the editor.
		void UpdatePrimitiveTransform()
		{
			primitiveTransform =
				glm::translate(glm::mat4(1.0f), primitivePosition) *
				glm::mat4_cast(glm::quat(primitiveRotation)) *
				glm::scale(glm::mat4(1.0f), primitiveScale);
			if (meshDrawable) meshDrawable->SetTransform(primitiveTransform);
		}

		//----------------------------------------------------------------------
		// Local-space bounds + the matrix that places them, for spatial queries
		// (Scene::Raycast). Returns false for anything that shouldn't be treated
		// as solid.
		//
		// Models carry real bounds from the GLTF import. Primitives never had
		// any — which is why they're excluded from frustum culling — but the
		// built-in geometry has *known* dimensions, so we can hand them out by
		// kind. That matters for stealth: hand-placed cover in an encampment is
		// made of TestCube primitives, and cover that doesn't block line of
		// sight isn't cover.
		//----------------------------------------------------------------------
		bool GetLocalBounds(glm::vec3& outMin, glm::vec3& outMax, glm::mat4& outTransform) const
		{
			if (model)
			{
				outMin = model->GetBoundsMin();
				outMax = model->GetBoundsMax();
				outTransform = model->GetTransform();
				return true;
			}

			switch (primitiveKind)
			{
			case PrimitiveKind::TestCube:
				// CreateTestCube builds a unit cube centred on the origin.
				outMin = glm::vec3(-0.5f);
				outMax = glm::vec3(0.5f);
				// Read the DRAWABLE's transform, not primitiveTransform. The
				// latter is only refreshed by the TRS accessors
				// (UpdatePrimitiveTransform), so code that drives an object by
				// calling meshDrawable->SetTransform directly — as the game does
				// for pawn bodies and cover — leaves primitiveTransform stale at
				// identity. Raycasting that gives phantom unit cubes at the world
				// origin while the real geometry goes untested. Always query what
				// is actually drawn.
				outTransform = meshDrawable ? meshDrawable->GetTransform() : primitiveTransform;
				return true;

			case PrimitiveKind::GroundPlane:
			case PrimitiveKind::MoonSphere:
				// Deliberately NOT occluders. The moon is sky decoration pinned
				// at the far plane. The ground plane is legacy test geometry
				// whose size is a creation-time parameter the scene doesn't
				// record — and terrain already answers "is the ground in the
				// way" via RayTerrain, which is what the game actually uses.
				return false;

			default:
				// Primitive geometry we can't reconstruct, or a drawable-less object.
				return false;
			}
		}

		// Convenience accessors for transform. Delegate to the Model for model-based
		// objects; edit the primitive TRS (and recompose) for primitives.
		glm::vec3 GetPosition() const
		{
			return model ? model->GetPosition() : primitivePosition;
		}

		glm::vec3 GetRotation() const
		{
			return model ? model->GetRotation() : primitiveRotation;
		}

		glm::vec3 GetScale() const
		{
			return model ? model->GetScale() : primitiveScale;
		}

		void SetPosition(const glm::vec3& pos)
		{
			if (model) { model->SetPosition(pos); return; }
			primitivePosition = pos;
			UpdatePrimitiveTransform();
		}

		void SetRotation(const glm::vec3& rot)
		{
			if (model) { model->SetRotation(rot); return; }
			primitiveRotation = rot;
			UpdatePrimitiveTransform();
		}

		void SetScale(const glm::vec3& scale)
		{
			if (model) { model->SetScale(scale); return; }
			primitiveScale = scale;
			UpdatePrimitiveTransform();
		}

		void SetScale(float uniform)
		{
			if (model) { model->SetScale(uniform); return; }
			primitiveScale = glm::vec3(uniform);
			UpdatePrimitiveTransform();
		}

		IDrawable* GetDrawable() const {
			if (drawable) return drawable.get();
			if (meshDrawable) return meshDrawable.get();
			return nullptr;
		}

		// Stats for inspector
		size_t GetMeshCount() const { return model ? model->GetMeshCount() : 0; }
		size_t GetVertexCount() const { return model ? model->GetTotalVertexCount() : 0; }
		size_t GetIndexCount() const { return model ? model->GetTotalIndexCount() : 0; }
	};

	//--------------------------------------------------------------------------
	// Scene - Container for SceneObjects with selection tracking
	//--------------------------------------------------------------------------
	class Scene
	{
	public:
		Scene() = default;
		~Scene() = default;

		// Add a model-based object to the scene
		SceneObject* AddObject(const std::string& name, std::unique_ptr<Model> model, Texture* defaultTexture = nullptr)
		{
			auto& obj = m_Objects.emplace_back();
			obj.name = name;
			obj.model = std::move(model);

			if (obj.model)
			{
				obj.drawable = std::make_unique<ModelDrawable>(obj.model.get(), defaultTexture);
			}

			return &m_Objects.back();
		}

		// Add a mesh drawable (for primitives like test cubes)
		// `kind` tags which built-in geometry this is. It is NOT cosmetic:
		// leaving it None makes the object invisible to Scene::Raycast (no
		// bounds are known, so GetLocalBounds refuses it) and unreconstructable
		// by SceneSerializer. A cover block added without a kind renders
		// perfectly and blocks nothing — which is exactly the bug that made
		// line-of-sight appear not to work at all.
		SceneObject* AddPrimitive(const std::string& name, std::unique_ptr<MeshDrawable> meshDrawable,
			PrimitiveKind kind = PrimitiveKind::None)
		{
			auto& obj = m_Objects.emplace_back();
			obj.name = name;
			obj.meshDrawable = std::move(meshDrawable);
			obj.primitiveKind = kind;
			return &m_Objects.back();
		}

		Light* AddLight(const std::string& name, LightType type = LightType::Directional)
		{
			Light light;
			light.name = name;
			light.type = type;
			m_Lights.push_back(light);
			return &m_Lights.back();
		}


		void Select(int index)
		{
			m_SelectedIndex = (index >= 0 && index < static_cast<int>(m_Objects.size()))
				? index : -1;
			m_SelectedLightIndex = -1;
		}

		void Deselect() { m_SelectedIndex = -1; }

		int GetSelectedIndex() const { return m_SelectedIndex; }

		SceneObject* GetSelected()
		{
			if (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_Objects.size()))
			{
				return &m_Objects[m_SelectedIndex];
			}
			return nullptr;
		}

		const SceneObject* GetSelected() const
		{
			if (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_Objects.size()))
			{
				return &m_Objects[m_SelectedIndex];
			}
			return nullptr;
		}

		// ADD AFTER:
		void SelectLight(int index)
		{
			m_SelectedLightIndex = (index >= 0 && index < static_cast<int>(m_Lights.size()))
				? index : -1;
			// Deselect object when selecting a light
			m_SelectedIndex = -1;
		}

		void DeselectLight() { m_SelectedLightIndex = -1; }

		int GetSelectedLightIndex() const { return m_SelectedLightIndex; }

		Light* GetSelectedLight()
		{
			if (m_SelectedLightIndex >= 0 && m_SelectedLightIndex < static_cast<int>(m_Lights.size()))
			{
				return &m_Lights[m_SelectedLightIndex];
			}
			return nullptr;
		}

		glm::vec3 GetAmbientColor() const { return m_AmbientColor; }
		float GetAmbientIntensity() const { return m_AmbientIntensity; }

		void SetAmbient(const glm::vec3& color, float intensity)
		{
			m_AmbientColor = color;
			m_AmbientIntensity = intensity;
		}

		// Iteration
		std::vector<SceneObject>& GetObjects() { return m_Objects; }
		const std::vector<SceneObject>& GetObjects() const { return m_Objects; }
		size_t GetObjectCount() const { return m_Objects.size(); }

		std::vector<Light>& GetLights() { return m_Lights; }
		const std::vector<Light>& GetLights() const { return m_Lights; }
		size_t GetLightCount() const { return m_Lights.size(); }

		SceneObject* GetObject(size_t index)
		{
			return index < m_Objects.size() ? &m_Objects[index] : nullptr;
		}

		Light* GetLight(size_t index)
		{
			return index < m_Lights.size() ? &m_Lights[index] : nullptr;
		}

		// Build draw list from all visible objects.
		// If a frustum is provided, model-based objects are culled against it
		// using their world-space AABB. Primitive objects (MeshDrawable) have
		// no bounds data yet and are always submitted.
		void BuildDrawList(DrawList& drawList, const Frustum* frustum = nullptr) const
		{
			m_LastObjectCount = 0;
			m_LastCulledCount = 0;

			for (const auto& obj : m_Objects)
			{
				if (!obj.visible) continue;

				auto* drawable = obj.GetDrawable();
				if (!drawable) continue;

				m_LastObjectCount++;

				// Camera-frustum cull. We do NOT drop culled objects from the list — they're
				// still submitted (marked cameraVisible=false) so the shadow and reflection
				// passes draw them. A caster behind/beside the camera still casts a shadow into
				// view; dropping it here is what made off-screen objects' shadows vanish.
				bool cameraVisible = true;
				if (frustum && obj.model)
				{
					glm::vec3 worldCenter, worldExtents;
					TransformAABB(
						obj.model->GetBoundsMin(), obj.model->GetBoundsMax(),
						obj.model->GetTransform(),
						worldCenter, worldExtents);

					if (!frustum->Intersects(worldCenter, worldExtents))
					{
						m_LastCulledCount++;
						cameraVisible = false;
					}
				}

				drawList.AddDrawable(drawable, cameraVisible);
			}
		}

		size_t GetLastObjectCount() const { return m_LastObjectCount; }
		size_t GetLastCulledCount() const { return m_LastCulledCount; }

		//----------------------------------------------------------------------
		// Spatial queries (Engine/Physics/Raycast.hpp)
		//----------------------------------------------------------------------

		// Nearest object hit along a ray, ignoring terrain (see HasLineOfSight
		// for the combined test). Brute-force over every object — fine at the
		// scene sizes we have, and honest: add a broadphase when a profile says
		// to, not before.
		//
		// `ignoreIndex` skips one object, so a guard can cast from inside its
		// own body without hitting itself.
		RayHit Raycast(const Ray& ray, float maxDistance, int ignoreIndex = -1) const
		{
			RayHit best;
			best.t = maxDistance;

			for (size_t i = 0; i < m_Objects.size(); ++i)
			{
				if (static_cast<int>(i) == ignoreIndex) continue;

				const auto& obj = m_Objects[i];
				if (!obj.visible || !obj.blocksSight) continue;

				glm::vec3 localMin, localMax;
				glm::mat4 transform;
				if (!obj.GetLocalBounds(localMin, localMax, transform)) continue;

				float     t = 0.0f;
				glm::vec3 normal(0.0f);
				if (RayOBB(ray, localMin, localMax, transform, best.t, t, normal) && t < best.t)
				{
					best.hit = true;
					best.t = t;
					best.point = ray.At(t);
					best.normal = normal;
					best.objectIndex = static_cast<int>(i);
					best.isTerrain = false;
				}
			}

			return best;
		}

		// Push a character out of any world geometry it is standing inside.
		// `position` is the FEET; returns a corrected feet position.
		//
		// Iterated a few times because resolving one box can push the character
		// into another (inside corners, or two crates flush together). Each pass
		// re-tests everything, so a corner converges instead of ping-ponging
		// between its two walls. Three passes is enough for the arrangements we
		// build by hand; it is a cap, not a guarantee.
		//
		// Y is never touched — vertical placement belongs to the ground sampler.
		// `stepHeight` splits walls from steps: a box whose top is within that
		// distance above the feet is walked ONTO (skipped here, reported by
		// SampleSupportHeight instead) rather than collided with. Without the
		// split every crate is an impassable wall.
		glm::vec3 ResolveCharacterCollision(const glm::vec3& position, float radius, float height,
			float stepHeight = 0.0f, int ignoreIndex = -1) const
		{
			glm::vec3 result = position;

			const int kPasses = 3;
			for (int pass = 0; pass < kPasses; ++pass)
			{
				bool moved = false;

				for (size_t i = 0; i < m_Objects.size(); ++i)
				{
					if (static_cast<int>(i) == ignoreIndex) continue;

					const auto& obj = m_Objects[i];
					if (!obj.visible || !obj.blocksMovement) continue;

					glm::vec3 localMin, localMax;
					glm::mat4 transform;
					if (!obj.GetLocalBounds(localMin, localMax, transform)) continue;

					AABB box = ComputeWorldAABB(localMin, localMax, transform);

					// Low enough to stand on: not an obstacle. Uses the ORIGINAL
					// feet height, not the running result, so a chain of pushes
					// can't change what counts as a step mid-resolve.
					if (IsSteppable(box, position.y, stepHeight)) continue;

					glm::vec3 push = ResolveCylinderVsAABB(result, radius, height, box);
					if (glm::dot(push, push) > 1e-10f)
					{
						result += push;
						moved = true;
					}
				}

				if (!moved) break;
			}

			return result;
		}

		// Highest surface the character can stand on at this position, or
		// -FLT_MAX if none. Terrain is NOT included — the caller combines this
		// with its own ground sampler, because terrain height is the app's
		// business and object tops are the scene's.
		//
		// `maxRise` bounds how far above the feet a surface may be and still be
		// stood on (the step height); it stops a wall's top counting as floor.
		float SampleSupportHeight(const glm::vec3& position, float radius, float maxRise,
			int ignoreIndex = -1) const
		{
			float best = -std::numeric_limits<float>::max();

			for (size_t i = 0; i < m_Objects.size(); ++i)
			{
				if (static_cast<int>(i) == ignoreIndex) continue;

				const auto& obj = m_Objects[i];
				if (!obj.visible || !obj.blocksMovement) continue;

				glm::vec3 localMin, localMax;
				glm::mat4 transform;
				if (!obj.GetLocalBounds(localMin, localMax, transform)) continue;

				AABB box = ComputeWorldAABB(localMin, localMax, transform);
				best = std::max(best, SurfaceHeightUnder(position, radius, box, maxRise));
			}

			return best;
		}

		// Find a ledge to mantle onto, looking from `feet` along `facing`.
		//
		// Deliberately simple: sample a few points ahead, take the highest solid
		// top that sits within the climbable band and has clear headroom. No
		// swept queries or edge detection — a handful of probes is enough to
		// find the top of a wall or crate, which is all the geometry we have.
		//
		// `outLedge` comes back as the position to STAND at, already nudged
		// past the lip so the pull-up doesn't end with the character balanced
		// exactly on the edge (where the next collision pass would shove them
		// straight back off).
		bool FindLedge(const glm::vec3& feet, const glm::vec3& facing,
			float reach, float maxHeight, float headroom,
			float characterRadius, glm::vec3& outLedge, int ignoreIndex = -1) const
		{
			glm::vec3 flat(facing.x, 0.0f, facing.z);
			if (glm::dot(flat, flat) < 1e-6f) return false;
			flat = glm::normalize(flat);

			bool  found = false;
			float bestTop = -std::numeric_limits<float>::max();
			glm::vec3 bestPos(0.0f);

			// Step outward so a thin wall and a deep crate both get sampled.
			const int kSamples = 4;
			for (int s = 1; s <= kSamples; ++s)
			{
				const float ahead = reach * (static_cast<float>(s) / kSamples) + characterRadius;
				const glm::vec3 probe = feet + flat * ahead;

				for (size_t i = 0; i < m_Objects.size(); ++i)
				{
					if (static_cast<int>(i) == ignoreIndex) continue;

					const auto& obj = m_Objects[i];
					if (!obj.visible || !obj.blocksMovement) continue;

					glm::vec3 localMin, localMax;
					glm::mat4 transform;
					if (!obj.GetLocalBounds(localMin, localMax, transform)) continue;

					AABB box = ComputeWorldAABB(localMin, localMax, transform);

					// Must be underneath the probe point horizontally.
					if (probe.x < box.min.x || probe.x > box.max.x) continue;
					if (probe.z < box.min.z || probe.z > box.max.z) continue;

					const float top = box.max.y;
					const float rise = top - feet.y;
					if (rise <= 0.0f || rise > maxHeight) continue;   // not a climbable ledge
					if (top <= bestTop) continue;

					// Headroom: nothing solid sitting on top of this ledge.
					bool blocked = false;
					for (size_t j = 0; j < m_Objects.size() && !blocked; ++j)
					{
						if (j == i || static_cast<int>(j) == ignoreIndex) continue;

						const auto& other = m_Objects[j];
						if (!other.visible || !other.blocksMovement) continue;

						glm::vec3 oMin, oMax; glm::mat4 oXf;
						if (!other.GetLocalBounds(oMin, oMax, oXf)) continue;

						AABB ob = ComputeWorldAABB(oMin, oMax, oXf);
						if (probe.x < ob.min.x || probe.x > ob.max.x) continue;
						if (probe.z < ob.min.z || probe.z > ob.max.z) continue;
						if (ob.min.y < top + headroom && ob.max.y > top) blocked = true;
					}
					if (blocked) continue;

					bestTop = top;
					// Land a little past the lip, so the character ends up on
					// the surface rather than teetering on its edge.
					bestPos = glm::vec3(probe.x, top, probe.z) + flat * (characterRadius * 0.5f);
					found = true;
				}
			}

			if (found) outLedge = bestPos;
			return found;
		}

		// Is `to` visible from `from`? The primitive the whole stealth layer
		// rests on (ROADMAP 3.3 vision cones).
		//
		// `groundFn` is the same injected height sampler the pawns take; pass
		// nullptr in a scene with no terrain. Terrain is tested first because
		// it's the cheaper of the two and, over real terrain, the more common
		// blocker — a hill between two points rejects before we touch objects.
		bool HasLineOfSight(const glm::vec3& from, const glm::vec3& to,
			const std::function<float(float, float)>& groundFn = nullptr,
			int ignoreIndex = -1) const
		{
			float length = 0.0f;
			Ray ray = Ray::Between(from, to, length);
			if (length <= 1e-6f) return true;   // same point: trivially visible

			// Pull the endpoint in slightly so a target resting exactly on the
			// terrain (or flush against a wall) doesn't register as blocked by
			// the very surface it's standing on.
			const float kEndpointEpsilon = 0.05f;
			float maxT = length - kEndpointEpsilon;
			if (maxT <= 0.0f) return true;

			if (groundFn)
			{
				float terrainT = 0.0f;
				if (RayTerrain(ray, groundFn, maxT, terrainT)) return false;
			}

			return !Raycast(ray, maxT, ignoreIndex).hit;
		}

		// Build GPU-ready lighting data from all enabled lights
		SceneLightingData BuildLightingData() const
		{
			SceneLightingData data;
			int count = 0;

			for (const auto& light : m_Lights)
			{
				if (!light.enabled) continue;
				if (count >= static_cast<int>(MAX_LIGHTS)) break;

				data.lights[count] = light.ToGPUData();
				count++;
			}

			data.numLights = count;
			data.ambient = glm::vec4(m_AmbientColor, m_AmbientIntensity);

			return data;
		}

		void Update(float deltaTime)
		{
			for (auto& obj : m_Objects)
			{
				if (obj.drawable)
				{
					obj.drawable->Update(deltaTime);
				}
				if (obj.meshDrawable)
				{
					obj.meshDrawable->Update(deltaTime);
				}
			}
		}

		// Remove object by index
		void RemoveObject(size_t index)
		{
			if (index < m_Objects.size())
			{
				m_Objects.erase(m_Objects.begin() + index);

				// Adjust selection
				if (m_SelectedIndex == static_cast<int>(index))
				{
					m_SelectedIndex = -1;
				}
				else if (m_SelectedIndex > static_cast<int>(index))
				{
					m_SelectedIndex--;
				}
			}
		}

		void RemoveLight(size_t index)
		{
			if (index < m_Lights.size())
			{
				m_Lights.erase(m_Lights.begin() + index);
			}
		}

		void Clear()
		{
			m_Objects.clear();
			m_SelectedIndex = -1;
		}

	private:
		std::vector<SceneObject> m_Objects;
		std::vector<Light> m_Lights;
		int m_SelectedIndex = -1;
		int m_SelectedLightIndex = -1;
		glm::vec3 m_AmbientColor = glm::vec3(0.03f, 0.03f, 0.05f);
		float m_AmbientIntensity = 1.0f;

		// Stats from the most recent BuildDrawList call (for debug display)
		mutable size_t m_LastObjectCount = 0;
		mutable size_t m_LastCulledCount = 0;
	};

} // namespace Nightbloom