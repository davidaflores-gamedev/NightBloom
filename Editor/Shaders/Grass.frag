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

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in float inHeightFraction;
layout(location = 3) in vec3 inTint;
layout(location = 4) in vec4 inShadowCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform FrameUBO
{
    mat4 view;
    mat4 proj;
    vec4 time;
    vec4 cameraPos;
} frame;

struct LightData {
    vec4 position;
    vec4 color;
    vec4 attenuation;
};

struct ShadowData {
    mat4 lightSpaceMatrix;
    vec4 shadowParams; // x = bias, y = normalBias, z = unused, w = enabled
};

layout(std140, set = 2, binding = 0) uniform LightingUBO
{
    LightData lights[16];
    vec4 ambient;
    int  numLights;
    int  _pad1, _pad2, _pad3;
    ShadowData shadowData;
} lighting;

layout(set = 3, binding = 0) uniform sampler2DShadow shadowMap;

float ComputeShadow(vec3 worldPos, vec3 normal, vec3 lightDir)
{
    float bias       = lighting.shadowData.shadowParams.x;
    float normalBias = lighting.shadowData.shadowParams.y;

    float cosTheta   = clamp(dot(normalize(normal), lightDir), 0.0, 1.0);
    float slopeScale = clamp(1.0 - cosTheta, 0.0, 1.0);
    vec3 biasedWorldPos = worldPos + normalize(normal) * (normalBias * slopeScale);

    vec4 lightSpacePos = lighting.shadowData.lightSpaceMatrix * vec4(biasedWorldPos, 1.0);
    if (abs(lightSpacePos.w) < 0.00001) return 1.0;

    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    float shadowDepth = proj.z;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
        shadowDepth < 0.0 || shadowDepth > 1.0)
        return 1.0;

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float currentDepth = shadowDepth - bias;

    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            shadow += texture(shadowMap, vec3(uv + offset, currentDepth));
        }

    return shadow / 9.0;
}

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
    float shadow = ComputeShadow(inWorldPos, Nshade, L);

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
    outColor = vec4(finalColor, 1.0);
}
