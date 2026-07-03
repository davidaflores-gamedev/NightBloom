//------------------------------------------------------------------------------
// Grass.vert
//
// Real instanced blade mesh (VertexPNT, see BladeMesh.hpp) — root at origin,
// local height 0..1, forward-curved with a baked per-row normal. Per-instance
// world position/rotation/scale/tint/wind-phase comes from the foliage storage
// buffer (set 1), indexed by gl_InstanceIndex, same raw-vec4-array pattern
// Firefly.vert uses for its agent buffer.
//
// Terrain height + slope are sampled from the same heightmap (set 4) Terrain
// itself uses, with the same finite-difference technique Terrain.vert uses
// for normals — there is no CPU-side height query in this engine, so all of
// this must happen here. Blades on slopes steeper than a threshold fade to
// a zero-size point at the root over a soft falloff band (cheap
// cliff-avoidance with no hard "wall" at the cutoff, no compute compaction
// needed).
//
// Distance visibility: the old width-boost hack (widening thin distant blades
// so they wouldn't sub-pixel-vanish) is RETIRED — it ballooned distant blades
// into fat triangles. MSAA on the scene pass plus higher placement density now
// keep thin distant blades reading correctly. pc.model[1].y/.z are inert.
//
// NOTE: Grass draws its blades at already-computed world positions (baked
// into the storage buffer at generation time), so it has no use for the
// push-constant model matrix as an actual transform. Instead `pc.model[0]`
// is repurposed to carry terrain-bounds data (worldSize, heightScale,
// terrain position XZ) and `pc.model[1]` carries (slopeFalloffCos,
// lodFirstInstance, lodDrawCountF, lodFadeBand) for the continuous distance
// LOD cross-fade, so GrassSystem doesn't need to grow the shared
// PushConstantData struct (which every other pipeline also uses).
// See GrassSystem::SubmitDraw.
//
// Descriptor set layout (matches VulkanPipelineAdapter's if-chain order for
// the Foliage pipeline config — see Renderer.cpp's Foliage pipeline block):
//   set 0 - FrameUniforms (view, proj, time, cameraPos)
//   set 1 - foliage instance storage buffer (vertex-only)
//   set 2 - lighting UBO (fragment only — unused here)
//   set 3 - shadow map (fragment only — unused here)
//   set 4 - heightmap (vertex stage — sampled here)
//------------------------------------------------------------------------------
#version 450

layout(location = 0) in vec3 inPosition;   // local blade space: x=tapered half-width, y=height fraction, z=0
layout(location = 1) in vec3 inNormal;     // unused — terrain finite-difference normal used instead
layout(location = 2) in vec2 inTexCoord;   // x = side (0=left,1=right), y = height fraction (matches inPosition.y)

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out float outHeightFraction;
layout(location = 3) out vec3 outTint;
layout(location = 4) out vec4 outShadowCoord;

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    vec4 time;
    vec4 cameraPos;
} frame;

layout(set = 1, binding = 0, std430) readonly buffer FoliageBuffer {
    vec4 data[];
} foliage;

layout(set = 4, binding = 0) uniform sampler2D heightmap;

layout(push_constant) uniform PushConstants
{
    mat4 model;       // model[0] repurposed: (terrainWorldSize, terrainHeightScale, terrainPosX, terrainPosZ)
                      // model[1] repurposed: (slopeFalloffCos, lodFirstInstance, lodDrawCountF, lodFadeBand)
    vec4 customData;  // x = slopeThresholdCos (upper bound), y = windStrength, z = windFrequency, w = windSpeed
} pc;

float SampleHeight(vec2 uv)
{
    uv = clamp(uv, vec2(0.001), vec2(0.999));
    return textureLod(heightmap, uv, 0.0).r;
}

// Coarse finite-difference slope/normal estimate. Grass doesn't need the
// pixel-accurate normal Terrain.vert computes (it has the heightmap's exact
// texel size available) — a fixed small step is sufficient for a binary
// "is this too steep to grow grass" test and a reasonable shading normal.
vec3 ComputeTerrainNormal(vec2 uv, float worldSize, float heightScale)
{
    const float kSampleStep = 1.0 / 256.0;

    float hL = SampleHeight(uv + vec2(-kSampleStep, 0.0));
    float hR = SampleHeight(uv + vec2( kSampleStep, 0.0));
    float hD = SampleHeight(uv + vec2(0.0, -kSampleStep));
    float hU = SampleHeight(uv + vec2(0.0,  kSampleStep));

    float worldStep = 2.0 * kSampleStep * worldSize;

    return normalize(vec3(
        (hL - hR) * heightScale,
        worldStep,
        (hD - hU) * heightScale
    ));
}

void main()
{
    float terrainWorldSize   = pc.model[0].x;
    float terrainHeightScale = pc.model[0].y;
    vec2  terrainPosXZ       = pc.model[0].zw;

    float slopeThresholdCos = pc.customData.x;
    float slopeFalloffCos   = pc.model[1].x;
    // Continuous distance-LOD cross-fade params (per patch, from
    // GrassSystem::SubmitDraw): the patch draws ceil(drawCountF) blades; the
    // top `fadeBand` of them (by instance order, which is a random priority
    // sort) scale-fade between 0 and 1 so the marginal blade grows/shrinks
    // smoothly as drawCountF slides with distance — no pop. (This slot used to
    // hold the retired width-boost hack.)
    float lodFirstInstance  = pc.model[1].y;
    float lodDrawCountF     = pc.model[1].z;
    float lodFadeBand       = pc.model[1].w;
    float windStrength      = pc.customData.y;
    float windFrequency     = pc.customData.z;
    float windSpeed         = pc.customData.w;

    uint idx = uint(gl_InstanceIndex);
    vec4 d0 = foliage.data[idx * 2u + 0u];
    vec4 d1 = foliage.data[idx * 2u + 1u];

    float worldX = d0.x;
    float worldZ = d0.y;
    float rotY   = d0.z;
    float scale  = d0.w;
    vec3  tint   = d1.xyz;
    float windPhase = d1.w;

    // Cross-fade this blade by its position in the drawn prefix.
    float localIdx = float(gl_InstanceIndex) - lodFirstInstance;
    float lodFade = 1.0 - smoothstep(lodDrawCountF - lodFadeBand, lodDrawCountF, localIdx);
    scale *= lodFade;

    vec2 uv = (vec2(worldX, worldZ) - terrainPosXZ) / terrainWorldSize + 0.5;
    float terrainY = SampleHeight(uv) * terrainHeightScale;
    vec3 terrainNormal = ComputeTerrainNormal(uv, terrainWorldSize, terrainHeightScale);

    // Don't grow grass underwater. frame.time.y = water surface Y, frame.time.z
    // = water-enabled flag (set by the Renderer when a WaterSystem is active).
    // Blades whose root is below the waterline fade out over a short band so the
    // shoreline isn't a hard ring — same collapse-to-root trick as the slope
    // cull below (cullFactor folds into the blade offset).
    float waterCull = 1.0;
    if (frame.time.z > 0.5)
    {
        float waterLevel = frame.time.y;
        waterCull = smoothstep(waterLevel - 0.2, waterLevel + 0.4, terrainY);
    }

    // Soft cutoff: smoothstep requires edge0 < edge1. slopeThresholdCos is
    // cos() of the steeper (threshold+halfFalloff) angle, so it's the
    // smaller value; slopeFalloffCos is cos() of the shallower
    // (threshold-halfFalloff) angle, the larger value — full grass at/above
    // slopeFalloffCos, fully gone at/below slopeThresholdCos, smooth S-curve
    // between. (Swapped order here once already — caused every blade to
    // collapse to zero scale, i.e. no grass visible at all.)
    float cullFactor = smoothstep(slopeThresholdCos, slopeFalloffCos, terrainNormal.y);
    cullFactor *= waterCull;

    // Y-rotation so instances face different directions. The blade mesh is now
    // curved (local z is nonzero — see BladeMesh.hpp), so rotate the FULL (x,z)
    // local position, not just x. (The old width-boost hack that ballooned
    // distant blades into fat triangles is gone — MSAA + density handle range
    // now; minApparentWidth/maxWidthBoost are inert, see GrassDesc.)
    float c = cos(rotY);
    float s = sin(rotY);
    vec3 rotated = vec3(
        inPosition.x * c + inPosition.z * s,
        inPosition.y,
        -inPosition.x * s + inPosition.z * c
    );

    // Rotate the baked blade normal the same way so fragment lighting uses the
    // blade's own (curve-derived, per-instance-oriented) normal, not the flat
    // terrain normal the old shader passed through.
    vec3 nLocal = normalize(inNormal);
    vec3 bladeNormal = normalize(vec3(
        nLocal.x * c + nLocal.z * s,
        nLocal.y,
        -nLocal.x * s + nLocal.z * c
    ));

    vec3 scaledLocal = rotated * scale;

    // Wind bends toward the tip (height fraction^2 weighting), blowing along
    // a fixed world +X direction — a v1 simplification, easily replaced with
    // a real wind direction parameter later.
    float windWeight = inTexCoord.y * inTexCoord.y;
    float windPhaseTotal = frame.time.x * windSpeed + worldX * windFrequency + windPhase;
    float windOffsetMag = sin(windPhaseTotal) * windStrength * windWeight * scale;
    vec3 windOffset = vec3(windOffsetMag, 0.0, 0.0);

    vec3 worldPos = vec3(
        worldX + (scaledLocal.x + windOffset.x) * cullFactor,
        terrainY + scaledLocal.y * cullFactor,
        worldZ + (scaledLocal.z + windOffset.z) * cullFactor
    );

    outWorldPos = worldPos;
    outNormal = bladeNormal;
    outHeightFraction = inTexCoord.y;
    outTint = tint;
    outShadowCoord = vec4(worldPos, 1.0);

    gl_Position = frame.proj * frame.view * vec4(worldPos, 1.0);
}
