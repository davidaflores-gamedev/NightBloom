//------------------------------------------------------------------------------
// Terrain.frag
//
// Fragment shader for GPU-displaced terrain.
// Receives interpolated world position and normal from Terrain.vert.
// Applies simple diffuse + specular lighting and PCF shadow lookup —
// matching the visual style of the Mesh pipeline.
//
// Descriptor sets:
//   set 0 - FrameUBO  (cameraPos)
//   set 1 - albedo texture (terrain colour / grass texture)
//   set 2 - SceneLightingData
//   set 3 - shadow map sampler
//   set 4 - heightmap (vertex stage — not used here)
//------------------------------------------------------------------------------

#version 450

// ---------------------------------------------------------------------------
// Inputs from vertex shader
// ---------------------------------------------------------------------------
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inShadowCoord;   // World position, passed through

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------
layout(location = 0) out vec4 outColor;

// ---------------------------------------------------------------------------
// Descriptor sets
// ---------------------------------------------------------------------------
layout(set = 0, binding = 0) uniform FrameUBO
{
    mat4 view;
    mat4 proj;
    vec4 time;
    vec4 cameraPos;
} frame;

layout(set = 1, binding = 0) uniform sampler2D uGrassTex;
layout(set = 1, binding = 1) uniform sampler2D uDirtTex;
layout(set = 1, binding = 2) uniform sampler2D uRockTex;

// Must match SceneLightingData in Light.hpp
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

// ---------------------------------------------------------------------------
// Push constants
// ---------------------------------------------------------------------------
layout(push_constant) uniform PushConstants
{
    mat4  model;
    vec4  customData;   // x = heightScale, y = texelSize, zw = unused
} pc;

// ---------------------------------------------------------------------------
// Shadow helper (PCF 3x3)
// ---------------------------------------------------------------------------
float ComputeShadow(vec3 worldPos, vec3 normal, vec3 lightDir)
{
    // Terrain's heightmap-driven slopes vary far more sharply over short
    // distances than the meshes these shadow bias values were tuned for,
    // so scale them up here to avoid self-shadowing acne shaped like the
    // underlying noise. Mesh.frag is untouched — it reads the same UBO.
    const float kTerrainBiasScale = 4.0;
    float bias       = lighting.shadowData.shadowParams.x * kTerrainBiasScale;
    float normalBias = lighting.shadowData.shadowParams.y * kTerrainBiasScale;

    // Slope-scaled normal bias — same as Mesh.frag
    float cosTheta   = clamp(dot(normalize(normal), lightDir), 0.0, 1.0);
    float slopeScale = clamp(1.0 - cosTheta, 0.0, 1.0);
    vec3 biasedWorldPos = worldPos + normalize(normal) * (normalBias * slopeScale);

    vec4 lightSpacePos = lighting.shadowData.lightSpaceMatrix * vec4(biasedWorldPos, 1.0);

    if (abs(lightSpacePos.w) < 0.00001) return 1.0;

    vec3  proj        = lightSpacePos.xyz / lightSpacePos.w;
    vec2  uv          = proj.xy * 0.5 + 0.5;
    float shadowDepth = proj.z;

    if (uv.x < 0.0 || uv.x > 1.0 ||
        uv.y < 0.0 || uv.y > 1.0 ||
        shadowDepth < 0.0 || shadowDepth > 1.0)
        return 1.0;

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));

    vec2 dpdx = dFdx(proj.xy);
    vec2 dpdy = dFdy(proj.xy);
    float dzdx = dFdx(proj.z);
    float dzdy = dFdy(proj.z);
    float receiverBias = abs(dzdx) * texelSize.x + abs(dzdy) * texelSize.y;
    receiverBias = clamp(receiverBias, 0.0, 0.02);
    float currentDepth = proj.z - bias - receiverBias;

    float shadow = 0.0;

    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            shadow += texture(shadowMap, vec3(uv + offset, currentDepth));
        }

    return shadow / 9.0;
}

// ---------------------------------------------------------------------------
// Terrain material blending
// ---------------------------------------------------------------------------
vec3 SampleTerrainAlbedo(vec3 worldPos, vec3 worldNormal)
{
    vec3 N = normalize(worldNormal);

    float tileScale = 0.08;
    vec2 uv = worldPos.xz * tileScale;

    vec3 grass = texture(uGrassTex, uv).rgb;
    vec3 dirt  = texture(uDirtTex,  uv).rgb;
    vec3 rock  = texture(uRockTex,  uv).rgb;

    float slope = 1.0 - dot(N, vec3(0,1,0));
    slope = clamp(slope * 1.5, 0.0, 1.0);

    float terrainBaseY = pc.model[3].y;
    float localHeight = worldPos.y - terrainBaseY;
    float normalizedHeight = clamp(localHeight / max(pc.customData.x, 0.001), 0.0, 1.0);

    float breakup = texture(uRockTex, uv * 0.02).r;
    breakup = (breakup - 0.5) * 0.15;

    float grassFactor =
    (1.0 - slope) *
    smoothstep(0.0, 0.5, 1.0 - normalizedHeight);

    float rockFactor =
    smoothstep(0.2, 0.6, slope) +
    smoothstep(0.6, 1.0, normalizedHeight);

    float dirtFactor =
    smoothstep(0.2, 0.5, normalizedHeight) *
    (1.0 - slope);  

    grassFactor += breakup;
    dirtFactor  -= breakup * 0.5;

    // sharpen
    grassFactor = pow(grassFactor, 3.5);
    dirtFactor  = pow(dirtFactor, 3.5);
    rockFactor  = pow(rockFactor, 3.5);

    grassFactor = max(grassFactor, 0.0);
    dirtFactor  = max(dirtFactor,  0.0);
    rockFactor  = max(rockFactor,  0.0);

    // normalize
    float total = grassFactor + dirtFactor + rockFactor;
    grassFactor /= total;
    dirtFactor  /= total;
    rockFactor  /= total;

    vec3 albedo =
        grass * grassFactor +
        dirt  * dirtFactor +
        rock  * rockFactor;

    return albedo;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
void main()
{
    vec3 N = normalize(inNormal);
    vec3 V = normalize(frame.cameraPos.xyz - inWorldPos);

    vec3 albedo = SampleTerrainAlbedo(inWorldPos, N);

    vec3 ambient = lighting.ambient.xyz * lighting.ambient.w * albedo.rgb;

    vec3 L = normalize(-lighting.lights[0].position.xyz);
    float NdotL = max(dot(N, L), 0.0);
    float shadow = ComputeShadow(inWorldPos, N, L);

    vec3 diffuse = lighting.lights[0].color.xyz * lighting.lights[0].color.w
                 * NdotL * shadow * albedo.rgb;

    vec3  H        = normalize(L + V);
    float spec     = pow(max(dot(N, H), 0.0), 32.0);
    vec3  specular = lighting.lights[0].color.xyz * lighting.lights[0].color.w
                   * spec * shadow * 0.1;

    vec3 pointContrib = vec3(0.0);
    for (int i = 0; i < lighting.numLights && i < 16; ++i)
    {
        // Skip directional lights (w == 0)
        if (lighting.lights[i].position.w < 0.5) continue;

        vec3  toLight = lighting.lights[i].position.xyz - inWorldPos;
        float dist    = length(toLight);
        float radius  = lighting.lights[i].attenuation.w;
        if (dist < radius)
        {
            vec3  Lp     = normalize(toLight);
            float att    = 1.0 - smoothstep(0.0, radius, dist);
            float NdotLp = max(dot(N, Lp), 0.05);
            pointContrib += lighting.lights[i].color.xyz
                          * lighting.lights[i].color.w
                          * NdotLp * att * albedo.rgb;
        }
    }

    vec3 finalColor = ambient + diffuse + specular + pointContrib;
    outColor = vec4(finalColor, 1.0);
}