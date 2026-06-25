//------------------------------------------------------------------------------
// Water.frag
//
// Reflective water surface (planar reflection v1). Combines:
//   - an animated surface normal (cheap "waves": scrolling sines, no vertex
//     displacement) that ripples both the reflection and the specular,
//   - a planar reflection sampled from the reflection target (the scene
//     re-rendered from the mirror-flipped camera — see Renderer::
//     RecordReflectionPass), projected by the fragment's screen position,
//   - a Schlick Fresnel blend between the reflection and the water body color
//     (reflective at grazing angles, see-through-ish color when looked at
//     straight down), and
//   - a Blinn-Phong sun specular using lighting.lights[0] (the same
//     directional light terrain/clouds read).
//
// Descriptor sets:
//   set 0 - FrameUniforms (scene camera)
//   set 1 - SceneLighting (sun for Fresnel reference + specular)
//   set 2 - reflection target sampler
//
// Deliberately deferred (Phase B/C): refraction / depth-based color, and
// panel-tunable deep/shallow colors (currently shader-side constants below).
//------------------------------------------------------------------------------
#version 450

// ---- Set 0: Frame Uniforms ----
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    vec4 time;
    vec4 cameraPos;
} frame;

// ---- Set 1: Scene Lighting ----
struct LightData {
    vec4 position;
    vec4 color;
    vec4 attenuation;
};
struct ShadowData {
    mat4 lightSpaceMatrix;
    vec4 shadowParams;
};
layout(std140, set = 1, binding = 0) uniform SceneLighting {
    LightData lights[16];
    vec4 ambient;
    int numLights;
    int _pad1, _pad2, _pad3;
    ShadowData shadowData;
} lighting;

// ---- Set 2: Planar reflection target ----
layout(set = 2, binding = 0) uniform sampler2D reflectionTex;

// ---- Push Constants ----
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 customData; // (waveAmplitude, waveSpeed, fresnelPower, alpha)
} push;

// ---- Inputs ----
layout(location = 0) in vec3 fragWorldPos;

// ---- Output ----
layout(location = 0) out vec4 outColor;

// Water body colors (shader-side defaults for v1; Phase C exposes these on the
// editor panel). deep = looking straight down, shallow tints the Fresnel mix.
const vec3 kDeepColor    = vec3(0.02, 0.10, 0.16);
const vec3 kShallowColor = vec3(0.10, 0.28, 0.34);

void main()
{
    float waveAmplitude = push.customData.x;
    float waveSpeed     = push.customData.y;
    float fresnelPower   = max(push.customData.z, 0.5);
    float alpha          = clamp(push.customData.w, 0.0, 1.0);

    // ---- Animated surface normal (the cheap "waves") ----
    // Perturb the flat up-normal with a couple of scrolling sine lobes over
    // world XZ. No vertex displacement — this only tilts the shading normal,
    // which is enough to ripple the reflection and the specular highlight.
    vec2 p = fragWorldPos.xz;
    float t = frame.time.x * waveSpeed;
    vec3 N = vec3(0.0, 1.0, 0.0);
    N.x += waveAmplitude * (sin(p.x * 0.50 + t) + 0.5 * sin(p.x * 0.23 - p.y * 0.31 + t * 1.3));
    N.z += waveAmplitude * (cos(p.y * 0.50 + t * 0.8) + 0.5 * sin(p.x * 0.17 + p.y * 0.40 + t * 0.9));
    N = normalize(N);

    vec3 V = normalize(frame.cameraPos.xyz - fragWorldPos);

    // ---- Planar reflection lookup ----
    // The reflection target was rendered at this same resolution; the fragment's
    // screen position indexes the matching reflected texel. The reflection pass
    // used a negative-height viewport (to fix mirror winding), which stores the
    // image vertically flipped — so undo that with (1 - v). If the reflection
    // ever shows up upside-down, flip this one line.
    vec2 reflUV = gl_FragCoord.xy / vec2(textureSize(reflectionTex, 0));
    reflUV.y = 1.0 - reflUV.y;
    // Ripple the lookup by the surface normal's horizontal tilt.
    reflUV += N.xz * 0.04;
    reflUV = clamp(reflUV, vec2(0.001), vec2(0.999));
    vec3 reflectionColor = texture(reflectionTex, reflUV).rgb;

    // ---- Fresnel (Schlick): reflective at grazing angles ----
    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    float fresnel = pow(1.0 - NdotV, fresnelPower);
    fresnel = clamp(fresnel, 0.02, 1.0); // small base reflectivity even head-on

    // Water body color (deep when looking down, shallower tint at grazing).
    vec3 bodyColor = mix(kDeepColor, kShallowColor, fresnel);

    vec3 color = mix(bodyColor, reflectionColor, fresnel);

    // ---- Sun specular (directional light 0) ----
    if (lighting.numLights > 0)
    {
        LightData sun = lighting.lights[0];
        if (sun.position.w < 0.5) // directional
        {
            vec3 L = normalize(-sun.position.xyz);
            vec3 H = normalize(L + V);
            float NdotH = max(dot(N, H), 0.0);
            float spec = pow(NdotH, 128.0);
            color += sun.color.rgb * sun.color.a * spec;
        }
    }

    outColor = vec4(color, alpha);
}
