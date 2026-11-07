//------------------------------------------------------------------------------
// DebugLine.vert
//
// Immediate-mode debug lines (Engine/Renderer/DebugDraw). World-space positions
// straight from a per-frame host-visible vertex buffer; no model matrix, no
// push constants. Vertex layout is the engine's shared VertexPCU shape
// (position, color, uv) — uv is unused.
//
// Descriptor sets:
//   set 0 - FrameUniforms (view, proj)
//------------------------------------------------------------------------------
#version 450

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    vec4 time;
    vec4 cameraPos;
} frame;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;   // unused, present to match the shared layout

layout(location = 0) out vec3 outColor;

void main()
{
    gl_Position = frame.proj * frame.view * vec4(inPosition, 1.0);
    outColor = inColor;
}
