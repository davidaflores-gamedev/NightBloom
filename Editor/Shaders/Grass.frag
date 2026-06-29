//------------------------------------------------------------------------------
// Grass.frag
//
// Procedural root->tip color gradient (no albedo texture in this pass —
// deferred, see .claude/ROADMAP.md Phase 1.5) tinted per-instance. Lit with
// the blade's own (curve-derived, per-instance) normal — biased toward
// world-up for a soft top-lit field — using half-Lambert wrap diffuse,
// a back-light translucency term (grass glows when the sun is behind it), a
// root-darkening fake AO, and the same PCF shadow lookup Terrain.frag uses
// (struct layouts copied verbatim — this codebase has no shared .glsl include
// mechanism). No specular, no point lights — not worth it for thin blades.
//
// Descriptor sets:
//   set 0 - FrameUBO (cameraPos — used for the two-sided view-facing flip)
//   set 1 - foliage storage buffer (vertex stage — unused here)
//   set 2 - SceneLightingData
//   set 3 - shadow map sampler
//   set 4 - heightmap (vertex stage — unused here)
//------------------------------------------------------------------------------
#version 450

#include "shadows.glsl"   // set 0 frame, set 2 lighting, set 3 shadowMap + CSM functions

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in float inHeightFraction;
layout(location = 3) in vec3 inTint;
layout(location = 4) in vec4 inShadowCoord;

layout(location = 0) out vec4 outColor;

void main()
{
    const vec3 kRootColor = vec3(0.08, 0.22, 0.05);
    const vec3 kTipColor  = vec3(0.46, 0.62, 0.20);

    vec3 albedo = mix(kRootColor, kTipColor, inHeightFraction) * inTint;

    // Up-biased normal. The baked blade normal always points up-ish (the
    // per-instance transform is a Y-rotation, which preserves the up
    // component), so we do NOT do a hard view-facing flip — a per-fragment
    // `if (dot(N,V)<0) N=-N` crosses its threshold partway up a curved blade
    // and paints a hard light/dark seam down the middle of it (the artifact
    // this replaced). Biasing smoothly toward world-up keeps the field reading
    // as a soft top-lit surface with gentle per-blade variation, no seam.
    vec3 N = normalize(inNormal);
    vec3 Nshade = normalize(mix(N, vec3(0.0, 1.0, 0.0), 0.4));

    vec3 L = normalize(-lighting.lights[0].position.xyz);
    vec3 lightColor = lighting.lights[0].color.xyz * lighting.lights[0].color.w;
    // Thin blades: no receiver-plane bias (pass 0), default bias scale.
    int cascade;
    float shadow = SampleShadow(inWorldPos, Nshade, L, 1.0, 0.0, cascade);

    // Half-Lambert (wrap) diffuse — softer falloff suits grass.
    float diffuse = clamp(dot(Nshade, L) * 0.5 + 0.5, 0.0, 1.0);
    diffuse *= diffuse;

    // Translucency / back-light glow: light passing THROUGH the blade when the
    // sun is behind it. The single biggest "real grass" cue with no texture.
    float backLight = clamp(dot(-Nshade, L), 0.0, 1.0);
    float transmission = pow(backLight, 2.0) * 0.6;

    // Fake AO: darker toward the root (less sky/light reaches between blades).
    float ao = mix(0.45, 1.0, inHeightFraction);

    vec3 ambient = lighting.ambient.xyz * lighting.ambient.w * albedo * ao;
    vec3 direct  = lightColor * albedo * (diffuse + transmission) * shadow * ao;

    vec3 finalColor = ambient + direct;
    outColor = vec4(ApplyCascadeDebug(finalColor, cascade), 1.0);
}
