//------------------------------------------------------------------------------
// SceneSerializer.cpp
//------------------------------------------------------------------------------

#include "Engine/Core/SceneSerializer.hpp"
#include "Engine/Core/Scene.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/Vulkan/VulkanTexture.hpp"  // complete type for VulkanTexture* -> Texture* upcast
#include "Engine/Core/Logger/Logger.hpp"

#include <nlohmann/json.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>

namespace Nightbloom
{
	using json = nlohmann::json;

	// Current scene-file schema version. Bump when the layout changes in a way
	// older loaders can't handle; Load() checks it and warns on mismatch.
	static constexpr int kSceneFileVersion = 1;

	//--------------------------------------------------------------------------
	// Small JSON helpers
	//--------------------------------------------------------------------------
	namespace
	{
		json Vec3ToJson(const glm::vec3& v) { return json::array({ v.x, v.y, v.z }); }

		glm::vec3 JsonToVec3(const json& a, const glm::vec3& def)
		{
			if (a.is_array() && a.size() == 3)
				return { a[0].get<float>(), a[1].get<float>(), a[2].get<float>() };
			return def;
		}

		glm::vec4 JsonToVec4(const json& a, const glm::vec4& def)
		{
			if (a.is_array() && a.size() == 4)
				return { a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>() };
			return def;
		}

		const char* PrimitiveKindToString(PrimitiveKind k)
		{
			switch (k)
			{
			case PrimitiveKind::TestCube:    return "TestCube";
			case PrimitiveKind::GroundPlane: return "GroundPlane";
			case PrimitiveKind::MoonSphere:  return "MoonSphere";
			default:                         return "None";
			}
		}

		PrimitiveKind PrimitiveKindFromString(const std::string& s)
		{
			if (s == "TestCube")    return PrimitiveKind::TestCube;
			if (s == "GroundPlane") return PrimitiveKind::GroundPlane;
			if (s == "MoonSphere")  return PrimitiveKind::MoonSphere;
			return PrimitiveKind::None;
		}

		// Resolve a primitive kind to its engine-owned built-in buffers.
		void GetPrimitiveBuffers(Renderer* r, PrimitiveKind k,
			Buffer*& vb, Buffer*& ib, uint32_t& indexCount)
		{
			vb = nullptr; ib = nullptr; indexCount = 0;
			switch (k)
			{
			case PrimitiveKind::TestCube:
				vb = r->GetTestVertexBuffer();  ib = r->GetTestIndexBuffer();  indexCount = r->GetTestIndexCount();
				break;
			case PrimitiveKind::GroundPlane:
				vb = r->GetGroundPlaneVertexBuffer(); ib = r->GetGroundPlaneIndexBuffer(); indexCount = r->GetGroundPlaneIndexCount();
				break;
			case PrimitiveKind::MoonSphere:
				vb = r->GetMoonSphereVertexBuffer(); ib = r->GetMoonSphereIndexBuffer(); indexCount = r->GetMoonSphereIndexCount();
				break;
			default:
				break;
			}
		}
	}

	//--------------------------------------------------------------------------
	// Save
	//--------------------------------------------------------------------------
	bool SceneSerializer::Save(const Scene& scene, const SceneCameraState& camera,
		const std::string& filepath, const std::string& sceneName,
		const std::string& editorStateJson)
	{
		json j;
		j["version"] = kSceneFileVersion;
		j["name"] = sceneName;

		j["camera"] = {
			{ "position", Vec3ToJson(camera.position) },
			{ "yaw", camera.yaw },
			{ "pitch", camera.pitch },
			{ "fov", camera.fov },
			{ "near", camera.nearPlane },
		};

		j["ambient"] = {
			{ "color", Vec3ToJson(scene.GetAmbientColor()) },
			{ "intensity", scene.GetAmbientIntensity() },
		};

		// --- Lights ---
		json lights = json::array();
		for (const Light& l : scene.GetLights())
		{
			const ShadowConfig& sc = l.shadowConfig;
			lights.push_back({
				{ "name", l.name },
				{ "type", static_cast<int>(l.type) },
				{ "enabled", l.enabled },
				{ "color", Vec3ToJson(l.color) },
				{ "intensity", l.intensity },
				{ "direction", Vec3ToJson(l.direction) },
				{ "position", Vec3ToJson(l.position) },
				{ "constant", l.constant },
				{ "linear", l.linear },
				{ "quadratic", l.quadratic },
				{ "radius", l.radius },
				{ "shadow", {
					{ "castsShadows", sc.castsShadows },
					{ "orthoSize", sc.orthoSize },
					{ "nearPlane", sc.nearPlane },
					{ "farPlane", sc.farPlane },
					{ "bias", sc.bias },
					{ "normalBias", sc.normalBias },
					{ "shadowDistance", sc.shadowDistance },
					{ "splitLambda", sc.splitLambda },
					{ "casterExtrude", sc.casterExtrude },
					{ "cascadeBlend", sc.cascadeBlend },
				}},
			});
		}
		j["lights"] = std::move(lights);

		// --- Objects ---
		json objects = json::array();
		for (const SceneObject& obj : scene.GetObjects())
		{
			if (obj.model)
			{
				objects.push_back({
					{ "kind", "model" },
					{ "name", obj.name },
					{ "visible", obj.visible },
					{ "source", obj.model->GetSourcePath() },
					{ "position", Vec3ToJson(obj.model->GetPosition()) },
					{ "rotation", Vec3ToJson(obj.model->GetRotation()) },  // euler radians
					{ "scale", Vec3ToJson(obj.model->GetScale()) },
				});
			}
			else if (obj.meshDrawable && obj.primitiveKind != PrimitiveKind::None)
			{
				objects.push_back({
					{ "kind", "primitive" },
					{ "name", obj.name },
					{ "visible", obj.visible },
					{ "primitive", PrimitiveKindToString(obj.primitiveKind) },
					{ "texture", obj.primitiveTexture },
					{ "pipeline", static_cast<int>(obj.pipeline) },
					{ "position", Vec3ToJson(obj.primitivePosition) },
					{ "rotation", Vec3ToJson(obj.primitiveRotation) },  // euler radians
					{ "scale", Vec3ToJson(obj.primitiveScale) },
					{ "customData", json::array({
						obj.meshDrawable->GetCustomData().x, obj.meshDrawable->GetCustomData().y,
						obj.meshDrawable->GetCustomData().z, obj.meshDrawable->GetCustomData().w }) },
				});
			}
			// Objects with neither a model nor a reconstructable primitive kind are
			// skipped — we can't rebuild them on load.
		}
		j["objects"] = std::move(objects);

		// Opaque editor state (panel settings). Stored as a real nested object so the
		// file stays human-readable, not an escaped string.
		if (!editorStateJson.empty())
		{
			try { j["editor"] = json::parse(editorStateJson); }
			catch (const std::exception& e)
			{
				LOG_WARN("SceneSerializer: editor state was not valid JSON, omitting: {}", e.what());
			}
		}

		std::ofstream out(filepath);
		if (!out.is_open())
		{
			LOG_ERROR("SceneSerializer: failed to open '{}' for writing", filepath);
			return false;
		}
		out << j.dump(2);
		out.close();

		LOG_INFO("SceneSerializer: saved scene '{}' to '{}' ({} objects, {} lights)",
			sceneName, filepath, scene.GetObjectCount(), scene.GetLightCount());
		return true;
	}

	//--------------------------------------------------------------------------
	// Load
	//--------------------------------------------------------------------------
	bool SceneSerializer::Load(Scene& scene, SceneCameraState& camera,
		const std::string& filepath, Renderer* renderer,
		std::string* outEditorStateJson)
	{
		if (!renderer)
		{
			LOG_ERROR("SceneSerializer::Load requires a valid renderer");
			return false;
		}

		std::ifstream in(filepath);
		if (!in.is_open())
		{
			LOG_ERROR("SceneSerializer: failed to open '{}' for reading", filepath);
			return false;
		}

		json j;
		try
		{
			in >> j;
		}
		catch (const std::exception& e)
		{
			LOG_ERROR("SceneSerializer: parse error in '{}': {}", filepath, e.what());
			return false;
		}

		int version = j.value("version", 0);
		if (version != kSceneFileVersion)
		{
			LOG_WARN("SceneSerializer: scene file version {} != expected {} — loading best-effort",
				version, kSceneFileVersion);
		}

		// Fully reset the scene. Scene::Clear only clears objects, so drop lights too.
		scene.Clear();
		while (scene.GetLightCount() > 0)
			scene.RemoveLight(scene.GetLightCount() - 1);

		// --- Camera ---
		if (j.contains("camera"))
		{
			const json& c = j["camera"];
			camera.position = JsonToVec3(c.value("position", json::array()), camera.position);
			camera.yaw = c.value("yaw", camera.yaw);
			camera.pitch = c.value("pitch", camera.pitch);
			camera.fov = c.value("fov", camera.fov);
			camera.nearPlane = c.value("near", camera.nearPlane);
		}

		// --- Ambient ---
		if (j.contains("ambient"))
		{
			const json& a = j["ambient"];
			scene.SetAmbient(
				JsonToVec3(a.value("color", json::array()), scene.GetAmbientColor()),
				a.value("intensity", scene.GetAmbientIntensity()));
		}

		// --- Lights ---
		for (const json& lj : j.value("lights", json::array()))
		{
			auto type = static_cast<LightType>(lj.value("type", 0));
			Light* l = scene.AddLight(lj.value("name", std::string("Light")), type);
			l->enabled = lj.value("enabled", true);
			l->color = JsonToVec3(lj.value("color", json::array()), l->color);
			l->intensity = lj.value("intensity", l->intensity);
			l->direction = JsonToVec3(lj.value("direction", json::array()), l->direction);
			l->position = JsonToVec3(lj.value("position", json::array()), l->position);
			l->constant = lj.value("constant", l->constant);
			l->linear = lj.value("linear", l->linear);
			l->quadratic = lj.value("quadratic", l->quadratic);
			l->radius = lj.value("radius", l->radius);
			if (lj.contains("shadow"))
			{
				const json& s = lj["shadow"];
				ShadowConfig& sc = l->shadowConfig;
				sc.castsShadows = s.value("castsShadows", sc.castsShadows);
				sc.orthoSize = s.value("orthoSize", sc.orthoSize);
				sc.nearPlane = s.value("nearPlane", sc.nearPlane);
				sc.farPlane = s.value("farPlane", sc.farPlane);
				sc.bias = s.value("bias", sc.bias);
				sc.normalBias = s.value("normalBias", sc.normalBias);
				sc.shadowDistance = s.value("shadowDistance", sc.shadowDistance);
				sc.splitLambda = s.value("splitLambda", sc.splitLambda);
				sc.casterExtrude = s.value("casterExtrude", sc.casterExtrude);
				sc.cascadeBlend = s.value("cascadeBlend", sc.cascadeBlend);
			}
		}

		// --- Objects ---
		ResourceManager* resources = renderer->GetResourceManager();
		Texture* defaultTex = resources ? resources->GetTexture("default_white") : nullptr;

		for (const json& oj : j.value("objects", json::array()))
		{
			const std::string kind = oj.value("kind", std::string());
			const std::string name = oj.value("name", std::string("Object"));
			const bool visible = oj.value("visible", true);

			if (kind == "model")
			{
				const std::string source = oj.value("source", std::string());
				if (source.empty())
				{
					LOG_WARN("SceneSerializer: model object '{}' has no source path — skipped", name);
					continue;
				}
				auto model = std::make_unique<Model>(name);
				if (!model->LoadFromFile(source, resources, renderer->GetDescriptorManager()))
				{
					LOG_WARN("SceneSerializer: failed to load model '{}' from '{}' — skipped", name, source);
					continue;
				}
				model->SetPosition(JsonToVec3(oj.value("position", json::array()), glm::vec3(0.0f)));
				model->SetRotation(JsonToVec3(oj.value("rotation", json::array()), glm::vec3(0.0f)));
				model->SetScale(JsonToVec3(oj.value("scale", json::array()), glm::vec3(1.0f)));
				SceneObject* o = scene.AddObject(name, std::move(model), defaultTex);
				if (o) o->visible = visible;
			}
			else if (kind == "primitive")
			{
				PrimitiveKind pk = PrimitiveKindFromString(oj.value("primitive", std::string("None")));
				Buffer* vb = nullptr; Buffer* ib = nullptr; uint32_t indexCount = 0;
				GetPrimitiveBuffers(renderer, pk, vb, ib, indexCount);
				if (!vb || !ib || indexCount == 0)
				{
					LOG_WARN("SceneSerializer: primitive '{}' ({}) has no buffers — skipped",
						name, oj.value("primitive", std::string("None")));
					continue;
				}
				auto pipeline = static_cast<PipelineType>(oj.value("pipeline", static_cast<int>(PipelineType::Mesh)));
				auto md = std::make_unique<MeshDrawable>(vb, ib, indexCount, pipeline);
				md->SetCustomData(JsonToVec4(oj.value("customData", json::array()), glm::vec4(0.0f)));

				const std::string texName = oj.value("texture", std::string());
				if (resources && !texName.empty())
				{
					if (Texture* tex = resources->GetTexture(texName))
						md->AddTexture(tex);
				}

				SceneObject* o = scene.AddPrimitive(name, std::move(md));
				if (o)
				{
					o->pipeline = pipeline;
					o->primitiveKind = pk;
					o->primitiveTexture = texName;
					o->visible = visible;
					o->primitivePosition = JsonToVec3(oj.value("position", json::array()), glm::vec3(0.0f));
					o->primitiveRotation = JsonToVec3(oj.value("rotation", json::array()), glm::vec3(0.0f));
					o->primitiveScale = JsonToVec3(oj.value("scale", json::array()), glm::vec3(1.0f));
					o->UpdatePrimitiveTransform();  // compose TRS -> drawable transform
				}
			}
			else
			{
				LOG_WARN("SceneSerializer: unknown object kind '{}' — skipped", kind);
			}
		}

		if (scene.GetObjectCount() > 0)
			scene.Select(0);

		if (outEditorStateJson)
			*outEditorStateJson = j.contains("editor") ? j["editor"].dump() : std::string("{}");

		LOG_INFO("SceneSerializer: loaded '{}' ({} objects, {} lights)",
			filepath, scene.GetObjectCount(), scene.GetLightCount());
		return true;
	}

} // namespace Nightbloom
