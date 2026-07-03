//------------------------------------------------------------------------------
// shadows.glsl
//
// Cascaded shadow map sampling, shared by every lit fragment shader (Mesh,
// Terrain, Grass, ...). Previously this logic was copy-pasted into each shader
// and had already drifted (terrain bias scale, mesh-only receiver-plane bias);
// it now lives here once. See .claude/CSM_DESIGN.md.
//
// Provides:
//   set 3, binding 0 -> sampler2DArrayShadow `shadowMap`
//   SelectCascade / CascadeRadius / ComputeShadow / SampleShadow / ApplyCascadeDebug
//
// Requires scene_common.glsl (frame + lighting blocks); pulled in below.
//------------------------------------------------------------------------------
#ifndef NB_SHADOWS_GLSL
#define NB_SHADOWS_GLSL

#include "scene_common.glsl"

// ---- Set 3: cascaded shadow map array (depth-compare sampler) ----
layout(set = 3, binding = 0) uniform sampler2DArrayShadow shadowMap;

// View-space forward distance of a world position (matches how cascades are split CPU-side).
float ViewDepth(vec3 worldPos)
{
    return -(frame.view * vec4(worldPos, 1.0)).z;
}

// Per-cascade FAR view-distance (the split planes). Component access avoids dynamic indexing.
float CascadeSplitDist(int c)
{
    if (c == 0) return lighting.shadowData.cascadeSplits.x;
    if (c == 1) return lighting.shadowData.cascadeSplits.y;
    if (c == 2) return lighting.shadowData.cascadeSplits.z;
    return lighting.shadowData.cascadeSplits.w;
}

// Pick the cascade by view-space depth; nearer cascades have smaller texels -> crisper.
// Loop form so it adapts to NUM_CASCADES (3 or 4) without out-of-range indexing.
int SelectCascade(vec3 worldPos)
{
    float d = ViewDepth(worldPos);
    for (int i = 0; i < NUM_CASCADES - 1; ++i)
        if (d < CascadeSplitDist(i)) return i;
    return NUM_CASCADES - 1;
}

// Per-cascade ortho half-size (world units) — used to scale bias for coarser far cascades.
float CascadeRadius(int c)
{
    if (c == 0) return lighting.shadowData.cascadeRadii.x;
    if (c == 1) return lighting.shadowData.cascadeRadii.y;
    if (c == 2) return lighting.shadowData.cascadeRadii.z;
    return lighting.shadowData.cascadeRadii.w;
}

// PCF 3x3 shadow lookup for one cascade.
//   biasScale        : per-surface multiplier (terrain wants ~4x; mesh/grass 1x)
//   maxReceiverBias  : clamp on the slope-adaptive receiver-plane bias; pass 0
//                      to disable it (thin geometry like grass blades).
float ComputeShadow(vec3 worldPos, vec3 normal, vec3 lightDir, int cascade,
                    float biasScale, float maxReceiverBias)
{
    if (lighting.shadowData.shadowParams.w < 0.5)
        return 1.0;

    // Far cascades have proportionally larger world-space texels, so scale bias
    // by this cascade's radius relative to cascade 0.
    float cascadeScale = CascadeRadius(cascade) / max(lighting.shadowData.cascadeRadii.x, 0.0001);

    // Depth bias scales LINEARLY with texel size (this is what kills acne in the coarse
    // far cascades — the depth slope error per texel grows with the texel).
    float bias = lighting.shadowData.shadowParams.x * biasScale * cascadeScale;

    // Normal bias scales SUB-LINEARLY (sqrt). Normal bias offsets the receiver laterally along
    // its surface normal; at the full linear ratio the far cascade can shove the sample point
    // ~0.5-2 world units sideways, so the SAME surface point lands at a different shadow lookup
    // in cascade c vs c+1. The blend band then cross-fades two differently-POSITIONED shadows,
    // which reads as the shadow "shifting"/doubling at the boundary. sqrt still clears the bigger
    // far texel (the reason any scaling exists) without the large lateral jump. Worst at grazing
    // angles, where slopeScale -> 1 — matching the observed artifact.
    float normalBias = lighting.shadowData.shadowParams.y * biasScale * sqrt(cascadeScale);

    // Slope-scaled normal bias — offset world pos along the surface normal before projecting.
    vec3  N          = normalize(normal);
    float cosTheta   = clamp(dot(N, lightDir), 0.0, 1.0);
    float slopeScale = clamp(1.0 - cosTheta, 0.0, 1.0);
    vec3  biasedWorldPos = worldPos + N * (normalBias * slopeScale);

    vec4 lightSpacePos = lighting.shadowData.lightSpaceMatrix[cascade] * vec4(biasedWorldPos, 1.0);
    if (abs(lightSpacePos.w) < 0.00001) return 1.0;

    vec3  proj = lightSpacePos.xyz / lightSpacePos.w;
    vec2  uv   = proj.xy * 0.5 + 0.5;
    float depth = proj.z;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
        depth < 0.0 || depth > 1.0)
        return 1.0;

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);

    // Receiver-plane bias — adapts to surface slope via screen-space derivatives.
    float receiverBias = 0.0;
    if (maxReceiverBias > 0.0)
    {
        float dzdx = dFdx(proj.z);
        float dzdy = dFdy(proj.z);
        receiverBias = clamp(abs(dzdx) * texelSize.x + abs(dzdy) * texelSize.y,
                             0.0, maxReceiverBias);
    }

    float currentDepth = depth - bias - receiverBias;

    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            shadow += texture(shadowMap, vec4(uv + offset, float(cascade), currentDepth));
        }

    return shadow / 9.0;
}

// Full cascaded lookup: selects the cascade, samples it, and cross-fades into
// the next cascade over the last 15% of the current one to hide the boundary
// pop. Returns the lit factor (1 = fully lit) and writes the chosen cascade to
// `cascadeOut` for debug visualization.
float SampleShadow(vec3 worldPos, vec3 normal, vec3 lightDir,
                   float biasScale, float maxReceiverBias, out int cascadeOut)
{
    int c = SelectCascade(worldPos);
    cascadeOut = c;

    float shadow = ComputeShadow(worldPos, normal, lightDir, c, biasScale, maxReceiverBias);

    // Cross-fade into the next cascade over the last `blend` fraction of this cascade's
    // view-depth band, to hide the texel-size/bias seam where an object straddles two cascades.
    if (c < NUM_CASCADES - 1)
    {
        float blend     = clamp(lighting.shadowData.extraParams.x, 0.0, 0.5);
        float d         = ViewDepth(worldPos);
        float nearD     = (c == 0) ? 0.0 : CascadeSplitDist(c - 1);
        float farD      = CascadeSplitDist(c);
        float bandStart = mix(nearD, farD, 1.0 - blend);
        if (d > bandStart)
        {
            float t = clamp((d - bandStart) / max(farD - bandStart, 0.0001), 0.0, 1.0);
            shadow = mix(shadow,
                         ComputeShadow(worldPos, normal, lightDir, c + 1, biasScale, maxReceiverBias),
                         t);
        }
    }
    return shadow;
}

// Debug: tint by cascade when shadowParams.z >= 0.5 (0=red,1=green,2=blue,3=yellow).
vec3 ApplyCascadeDebug(vec3 color, int cascade)
{
    if (lighting.shadowData.shadowParams.z < 0.5) return color;
    vec3 tint = (cascade == 0) ? vec3(1.0, 0.4, 0.4)
              : (cascade == 1) ? vec3(0.4, 1.0, 0.4)
              : (cascade == 2) ? vec3(0.4, 0.4, 1.0)
                               : vec3(1.0, 1.0, 0.4);
    return mix(color, color * tint, 0.6);
}

#endif // NB_SHADOWS_GLSL
