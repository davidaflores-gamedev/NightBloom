#pragma once

//------------------------------------------------------------------------------
// EditorDemo.hpp
//
// Scripted "demo mode" for the editor, used to record repeatable showcase shots
// (see Tools/Reel). Launch with:
//
//     Editor.exe --demo <shot.json>
//
// The editor then ignores the mouse/keyboard camera, flies a keyframed
// CameraPath, sets time of day, toggles debug views and pokes panel parameters
// on a timeline, and quits when the shot ends. Nothing here runs, and nothing
// is saved differently, unless --demo is passed.
//
// Timeline: `warmup` seconds first (every panel is drawn so every environment
// system initializes, shaders warm up, terrain regenerates), then t = 0. At
// t = 0 the editor writes the signal file (--demo-signal <path>) so a recorder
// can start exactly on the first real frame of the shot.
//
// Shot file (JSON), all fields optional except camera:
//   warmup        seconds before t = 0                    (default 4)
//   duration      seconds after t = 0 before quitting     (default: camera length)
//   ui            show editor UI during the shot          (default false)
//   panels        when ui = true, the panels left open, by View-menu name
//   layout        when ui = true: { "<panel>": [x, y, w, h], ... } pixel rects,
//                 so a UI shot is framed the same on any machine (no imgui.ini)
//   fov           vertical field of view, degrees         (default 45)
//   timeOfDay     hours [0, 24)
//   autoAdvance   bool;  speedHours  game-hours per second
//   shadowResolution   1024 | 2048 | 4096
//   cascadeDebug  [[t0, t1], ...]  windows with the cascade tint on
//   camera        [{ t, pos:[x,y,z], lookAt:[x,y,z] | yaw, pitch }, ...]
//   groundRelative   true: every pos.y / lookAt.y is metres ABOVE THE TERRAIN at
//                    that x,z (the editor's CPU heightfield), so shots can hug
//                    the ground without knowing its height        (default false)
//   minClearance     never let the camera dip closer than this to the terrain
//                    between keys, metres (default 1.0; -1 disables)
//   terrain       [{ t, seed, frequency }, ...]       live terrain edits
//   select        [{ t, object }, ...]                scene selection changes
//   clouds        { coverage, shapeScale, detailScale, detailStrength,
//                   densityMultiplier, extinctionCoefficient, hgAnisotropy,
//                   stepCount, resolutionScale, windSpeed, windDirection:[x,y,z],
//                   layerMinY, layerMaxY }
//   water         { waterY, waveAmplitude, waveSpeed, fresnelPower, alpha }
//                 Both apply at t = 0, exactly like moving the panel's sliders.
//------------------------------------------------------------------------------

#include "Engine/Core/CameraPath.hpp"
#include "Engine/Core/Logger/Logger.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <functional>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace Nightbloom
{
	struct EditorDemoScript
	{
		struct TerrainEdit { float t = 0.0f; int seed = -1; float frequency = -1.0f; };
		struct SelectEdit  { float t = 0.0f; int object = -1; };
		struct RawKey
		{
			float     t = 0.0f;
			glm::vec3 pos = glm::vec3(0.0f);
			bool      hasLookAt = false;
			glm::vec3 lookAt = glm::vec3(0.0f);
			float     yaw = 0.0f, pitch = 0.0f;
		};

		std::string              name;
		float                    warmup = 4.0f;
		float                    duration = 0.0f;
		bool                     ui = false;
		std::vector<std::string> panels;
		std::vector<std::pair<std::string, glm::vec4>> layout;   // name -> x, y, w, h
		float                    fov = 45.0f;
		bool                     hasTimeOfDay = false;
		float                    timeOfDay = 22.0f;
		bool                     autoAdvance = false;
		float                    speedHours = 1.0f;
		int                      shadowResolution = 0;
		std::vector<std::pair<float, float>> cascadeDebug;
		std::vector<RawKey>      keys;
		bool                     groundRelative = false;
		float                    minClearance = 1.0f;
		CameraPath               camera;   // built from keys by BuildCamera()
		nlohmann::json           clouds;   // slider overrides, applied at t = 0
		nlohmann::json           water;
		std::vector<TerrainEdit> terrain;
		std::vector<SelectEdit>  select;

		bool PanelVisible(const char* panelName) const
		{
			for (const auto& p : panels) if (p == panelName) return true;
			return false;
		}

		bool CascadeDebugAt(float t) const
		{
			for (const auto& w : cascadeDebug) if (t >= w.first && t < w.second) return true;
			return false;
		}

		// Resolve the raw keys into the camera path. With groundRelative, heights
		// are offsets above the terrain, so this needs the terrain to be ready;
		// before that (warmup) pass no heightAt and keys are taken as absolute.
		void BuildCamera(const std::function<float(float, float)>& heightAt)
		{
			camera.Clear();
			const bool ground = groundRelative && heightAt;
			for (const RawKey& k : keys)
			{
				CameraKey key;
				key.time = k.t;
				key.position = k.pos;
				if (ground) key.position.y += heightAt(k.pos.x, k.pos.z);
				if (k.hasLookAt)
				{
					glm::vec3 target = k.lookAt;
					if (ground) target.y += heightAt(k.lookAt.x, k.lookAt.z);
					CameraPath::LookAtAngles(key.position, target, key.yaw, key.pitch);
				}
				else
				{
					key.yaw = k.yaw;
					key.pitch = k.pitch;
				}
				camera.AddKey(key);
			}
		}

		static bool Load(const std::filesystem::path& file, EditorDemoScript& out)
		{
			std::ifstream in(file);
			if (!in) { LOG_ERROR("Demo: cannot open '{}'", file.string()); return false; }

			nlohmann::json j;
			try { in >> j; }
			catch (const std::exception& e)
			{
				LOG_ERROR("Demo: '{}' is not valid JSON: {}", file.string(), e.what());
				return false;
			}

			auto vec3 = [](const nlohmann::json& a) {
				return glm::vec3(a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>());
			};

			try
			{
				out = EditorDemoScript{};
				out.name     = file.stem().string();
				out.warmup   = j.value("warmup", 4.0f);
				out.ui       = j.value("ui", false);
				out.fov      = j.value("fov", 45.0f);
				if (j.contains("panels")) out.panels = j["panels"].get<std::vector<std::string>>();
				if (j.contains("layout"))
					for (const auto& [panel, r] : j["layout"].items())
						out.layout.emplace_back(panel, glm::vec4(r.at(0).get<float>(), r.at(1).get<float>(),
							r.at(2).get<float>(), r.at(3).get<float>()));
				if (j.contains("timeOfDay")) { out.hasTimeOfDay = true; out.timeOfDay = j["timeOfDay"].get<float>(); }
				out.autoAdvance      = j.value("autoAdvance", false);
				out.speedHours       = j.value("speedHours", 1.0f);
				out.shadowResolution = j.value("shadowResolution", 0);

				if (j.contains("cascadeDebug"))
					for (const auto& w : j["cascadeDebug"])
						out.cascadeDebug.emplace_back(w.at(0).get<float>(), w.at(1).get<float>());

				if (j.contains("clouds")) out.clouds = j["clouds"];
				if (j.contains("water"))  out.water  = j["water"];
				out.groundRelative = j.value("groundRelative", false);
				out.minClearance   = j.value("minClearance", 1.0f);
				for (const auto& k : j.at("camera"))
				{
					RawKey key;
					key.t = k.at("t").get<float>();
					key.pos = vec3(k.at("pos"));
					if (k.contains("lookAt")) { key.hasLookAt = true; key.lookAt = vec3(k["lookAt"]); }
					key.yaw = k.value("yaw", 0.0f);
					key.pitch = k.value("pitch", 0.0f);
					out.keys.push_back(key);
				}
				out.BuildCamera(nullptr);

				if (j.contains("terrain"))
					for (const auto& e : j["terrain"])
						out.terrain.push_back({ e.at("t").get<float>(), e.value("seed", -1), e.value("frequency", -1.0f) });

				if (j.contains("select"))
					for (const auto& e : j["select"])
						out.select.push_back({ e.at("t").get<float>(), e.at("object").get<int>() });

				out.duration = j.value("duration", out.camera.Duration());
			}
			catch (const std::exception& e)
			{
				LOG_ERROR("Demo: '{}' has a bad field: {}", file.string(), e.what());
				return false;
			}

			if (out.camera.Empty())
			{
				LOG_ERROR("Demo: '{}' has no camera keys", file.string());
				return false;
			}
			return true;
		}
	};

} // namespace Nightbloom
