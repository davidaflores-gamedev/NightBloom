//------------------------------------------------------------------------------
// PostProcess.vert
//
// Full-screen triangle (3 verts, no vertex/index buffer — gl_VertexIndex
// only, same pattern Clouds.vert/Firefly.vert use). This pass has no depth
// attachment at all (see RenderPassManager::CreatePostProcessRenderPass),
// so the z value written here is irrelevant — only here for a valid
// clip-space position.
//------------------------------------------------------------------------------
#version 450

layout(location = 0) out vec2 outUV;

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
    outUV = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
