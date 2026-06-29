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

#include "shadows.glsl"   // set 0 frame, set 2 lighting, set 3 shadowMap + CSM functions

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
layout(set = 1, binding = 0) uniform sampler2D uGrassTex;
layout(set = 1, binding = 1) uniform sampler2D uDirtTex;
layout(set = 1, binding = 2) uniform sampler2D uRockTex;

// ---------------------------------------------------------------------------
// Push constants
// ---------------------------------------------------------------------------
layout(push_constant) uniform PushConstants
{
    mat4  model;
    vec4  customData;   // x = heightScale, y = texelSize, zw = unused
} pc;

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
    // Terrain wants ~4x bias (sharp heightmap slopes) + a larger receiver-bias clamp.
    int cascade;
    float shadow = SampleShadow(inWorldPos, N, L, 4.0, 0.02, cascade);

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
    outColor = vec4(ApplyCascadeDebug(finalColor, cascade), 1.0);
}