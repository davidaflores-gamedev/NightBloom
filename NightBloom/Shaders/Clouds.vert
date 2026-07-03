//------------------------------------------------------------------------------
// Clouds.vert
//
// Full-screen triangle (3 verts, no vertex/index buffer — gl_VertexIndex
// only, same no-geometry-buffer pattern Firefly.vert uses). Outputs a fixed
// "at infinity" depth (Vulkan NDC z=0, this engine's reverse-Z far value) —
// the standard skybox/sky-layer trick: since this draws inside the main pass
// after opaque/terrain have already written real depth, the normal GPU
// depth test (GreaterOrEqual) discards cloud pixels wherever anything closer
// was already drawn, occluding clouds behind terrain/mountains for free.
//------------------------------------------------------------------------------
#version 450

layout(location = 0) out vec2 outNDC;

// One big triangle covering the whole [-1,1] NDC square; the two far
// corners are outside the viewport and get clipped by the hardware.
const vec2 kPositions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main()
{
    vec2 pos = kPositions[gl_VertexIndex];
    outNDC = pos;
    gl_Position = vec4(pos, 0.0, 1.0); // z=0 -> far plane in this engine's reverse-Z convention
}
