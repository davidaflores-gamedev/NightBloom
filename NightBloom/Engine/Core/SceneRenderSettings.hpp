//------------------------------------------------------------------------------
// SceneRenderSettings.hpp
//
// Glue between what a scene file asks for (Scene.hpp: SceneEnvironment /
// ScenePostProcess) and the live Renderer. Every app that loads a scene needs
// this mapping; it lives here so the editor, the fighter test and the game
// runtime share ONE field-by-field copy instead of each keeping their own.
//
// Header-only. Scene.hpp deliberately doesn't include Renderer.hpp, so the
// bridge sits beside both.
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Core/Scene.hpp"
#include "Engine/Renderer/Renderer.hpp"

namespace Nightbloom
{
	// Scene file -> renderer (on scene load / new scene).
	inline void ApplyScenePostProcess(Renderer& renderer, const ScenePostProcess& s)
	{
		auto& pp = renderer.GetPostProcessSettings();
		pp.edgeBlurEnabled  = s.edgeBlur;
		pp.tonemapEnabled   = s.tonemap;
		pp.exposure         = s.exposure;
		pp.vignetteStrength = s.vignetteStrength;
		pp.bloomIntensity   = s.bloomIntensity;
		pp.bloomThreshold   = s.bloomThreshold;
	}

	// Renderer -> scene file (before saving, so Debug-panel tweaks persist).
	inline void CaptureScenePostProcess(const Renderer& renderer, ScenePostProcess& s)
	{
		const auto& pp = renderer.GetPostProcessSettings();
		s.edgeBlur         = pp.edgeBlurEnabled;
		s.tonemap          = pp.tonemapEnabled;
		s.exposure         = pp.exposure;
		s.vignetteStrength = pp.vignetteStrength;
		s.bloomIntensity   = pp.bloomIntensity;
		s.bloomThreshold   = pp.bloomThreshold;
	}
}
