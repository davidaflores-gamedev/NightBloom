//------------------------------------------------------------------------------
// scene_common.glsl
//
// Shared frame + lighting descriptor blocks for fragment shaders. Included
// (directly or via shadows.glsl) by every lit pass so the FrameUBO / LightData /
// ShadowData / SceneLighting layouts live in ONE place instead of being copied
// per shader. Must stay byte-for-byte in sync with the CPU structs in
// Light.hpp (SceneLightingData) and FrameUniformData.
//
// Provides:
//   set 0, binding 0  -> FrameUBO        instance `frame`
//   set 2, binding 0  -> SceneLighting   instance `lighting`
//
// Set 1 (textures / storage) is pass-specific and stays in each shader.
//------------------------------------------------------------------------------
#ifndef NB_SCENE_COMMON_GLSL
#define NB_SCENE_COMMON_GLSL

// Number of shadow cascades. Mirrors Nightbloom::NUM_CASCADES in Light.hpp —
// keep the two in sync (the CPU side is the canonical definition). Max 4.
const int NUM_CASCADES = 4;
const int MAX_LIGHTS   = 16;

// ---- Set 0: per-frame camera/time ----
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec4 time;
    vec4 cameraPos;
} frame;

// ---- Set 2: scene lighting (must match SceneLightingData in Light.hpp) ----
struct LightData {
    vec4 position;     // xyz = pos/dir, w = type (0 = directional, 1 = point)
    vec4 color;        // rgb = color, a = intensity
    vec4 attenuation;  // x=constant, y=linear, z=quadratic, w=radius
};

struct ShadowData {
    mat4 lightSpaceMatrix[NUM_CASCADES];   // one light VP per cascade
    vec4 cascadeSplits;                    // view-space FAR distance of each cascade (selection)
    vec4 cascadeRadii;                     // ortho half-size of each cascade (bias scaling)
    vec4 shadowParams;                     // x=bias, y=normalBias, z=debugTint, w=enabled
    vec4 extraParams;                      // x=cascade blend fraction, yzw=reserved
};

layout(std140, set = 2, binding = 0) uniform SceneLighting {
    LightData  lights[MAX_LIGHTS];
    vec4       ambient;     // rgb = color, a = intensity
    int        numLights;
    int        _pad1, _pad2, _pad3;
    ShadowData shadowData;
} lighting;

#endif // NB_SCENE_COMMON_GLSL
