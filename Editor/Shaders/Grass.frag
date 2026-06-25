//------------------------------------------------------------------------------
// Grass.frag
//
// Procedural root->tip color gradient (no albedo texture in this pass —
// deferred, see .claude/ROADMAP.md Phase 1.5) tinted per-instance, lit with
// ambient + directional diffuse and the same PCF shadow lookup Terrain.frag
// uses (struct layouts copied verbatim — this codebase has no shared .glsl
// include mechanism). No specular, no point lights — not worth it for thin
// blades at this density.
//
// Descriptor sets:
//   set 0 - FrameUBO (cameraPos — unused here, kept for layout consistency)
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
    const vec3 kRootColor = vec3(0.10, 0.28, 0.07);
    const vec3 kTipColor  = vec3(0.45, 0.55, 0.18);

    vec3 albedo = mix(kRootColor, kTipColor, inHeightFraction) * inTint;

    vec3 N = normalize(inNormal);
    vec3 ambient = lighting.ambient.xyz * lighting.ambient.w * albedo;

    vec3 L = normalize(-lighting.lights[0].position.xyz);
    float NdotL = max(dot(N, L), 0.0);
    float shadow = ComputeShadow(inWorldPos, N, L);

    vec3 diffuse = lighting.lights[0].color.xyz * lighting.lights[0].color.w
                 * NdotL * shadow * albedo;

    vec3 finalColor = ambient + diffuse;
    outColor = vec4(finalColor, 1.0);
}
