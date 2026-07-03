//------------------------------------------------------------------------------
// Terrain.vert
//
// Vertex shader for GPU-displaced terrain.
// Input: flat NxN grid with Y=0 (VertexPNT layout)
// Displacement: samples heightmap at (texCoord) in vertex stage
// Normals: computed via finite differences on the heightmap
//
// Descriptor set layout (matches Terrain pipeline layout):
//   set 0 - FrameUniform  (view, proj, time, cameraPos)
//   set 1 - albedo texture (fragment only — unused here)
//   set 2 - lighting UBO   (fragment only — unused here)
//   set 3 - shadow map     (fragment only — unused here)
//   set 4 - heightmap      (vertex stage — sampled here)
//------------------------------------------------------------------------------

#version 450

// ---------------------------------------------------------------------------
// Vertex inputs (VertexPNT layout)
// ---------------------------------------------------------------------------
layout(location = 0) in vec3 inPosition;   // XZ grid position, Y = 0
layout(location = 1) in vec3 inNormal;     // Unused — normals computed here
layout(location = 2) in vec2 inTexCoord;   // [0,1] UV over the terrain patch

// ---------------------------------------------------------------------------
// Outputs to fragment shader
// ---------------------------------------------------------------------------
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec4 outShadowCoord;

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

// set 1,2,3 are bound but not accessed in this stage

// Heightmap — vertex-stage sampler (set 4)
layout(set = 4, binding = 0) uniform sampler2D heightmap;

// ---------------------------------------------------------------------------
// Push constants
// ---------------------------------------------------------------------------
layout(push_constant) uniform PushConstants
{
    mat4  model;
    vec4  customData;   // x = heightScale, y = texelSize (1/gridResolution), zw = unused
} pc;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

float SampleHeight(vec2 uv)
{
    // Clamp to avoid bleeding at edges
    uv = clamp(uv, vec2(0.001), vec2(0.999));
    return textureLod(heightmap, uv, 0.0).r;  // force mip 0
}

vec3 ComputeNormal(vec2 uv, float texelSize, float heightScale, float worldSize)
{
    float sampleStep = texelSize * 2.0;

    float hL = SampleHeight(uv + vec2(-sampleStep, 0.0));
    float hR = SampleHeight(uv + vec2( sampleStep, 0.0));
    float hD = SampleHeight(uv + vec2(0.0, -sampleStep));
    float hU = SampleHeight(uv + vec2(0.0,  sampleStep));

    // World-space horizontal distance between the two sample points
    float worldStep = 2.0 * sampleStep * worldSize;

    vec3 n = normalize(vec3(
        (hL - hR) * heightScale,
        worldStep,              // correct denominator
        (hD - hU) * heightScale
    ));
    return n;
}

// ---------------------------------------------------------------------------
// Shadow coordinate (matches ShadowMapManager light-space matrix convention)
// ---------------------------------------------------------------------------
// The shadow matrix is baked into customData.zw — but we don't have the full
// 4x4 light matrix here. Pass the world position to the fragment shader and
// let it reconstruct the shadow lookup via the shadow UBO. This matches how
// the Mesh pipeline works.

void main()
{
    float heightScale      = pc.customData.x;
    float texelSize        = pc.customData.y;  // grid spacing (for displacement) // DEPRICATED?
    float worldSize        = pc.customData.z;
    float hmapTexelSize    = pc.customData.w;  // heightmap spacing (for normals)

    // Sample height and displace vertex up
    float h = SampleHeight(inTexCoord) * heightScale;
    vec3 displacedPos = inPosition + vec3(0.0, h, 0.0);

    // Compute world-space position
    vec4 worldPos4 = pc.model * vec4(displacedPos, 1.0);
    outWorldPos  = worldPos4.xyz;
    outTexCoord  = inTexCoord;

    // Compute normal in object space, then transform to world space
    vec3 objNormal = ComputeNormal(inTexCoord, hmapTexelSize, heightScale, worldSize);
    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    outNormal = normalize(normalMatrix * objNormal);

    // Shadow coordinate — pass world position; fragment shader handles projection
    outShadowCoord = worldPos4;  // Fragment shader will apply light matrix

    gl_Position = frame.proj * frame.view * worldPos4;
}
