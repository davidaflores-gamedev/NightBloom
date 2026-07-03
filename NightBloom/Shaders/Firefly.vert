//------------------------------------------------------------------------------
// Firefly.vert
//
// Camera-facing billboard quad for one firefly agent. No vertex/index buffer —
// the quad is generated procedurally from gl_VertexIndex (6 verts/instance,
// 2 triangles), and per-agent data is read from the storage buffer (set 1)
// by gl_InstanceIndex. Camera right/up are derived from the view matrix's
// basis columns rather than passed in separately.
//
// Descriptor sets:
//   set 0 - FrameUniforms (view, proj)
//   set 1 - firefly agent storage buffer (vertex+compute visible)
//------------------------------------------------------------------------------
#version 450

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    vec4 time;
    vec4 cameraPos;
} frame;

layout(set = 1, binding = 0, std430) readonly buffer AgentBuffer {
    vec4 data[];
} agentBuffer;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

// Two triangles, CCW: (0,1,2) and (0,2,3) over corners
//   0:(-0.5,-0.5) 1:(-0.5,0.5) 2:(0.5,0.5) 3:(0.5,-0.5)
const vec2 kCorners[4] = vec2[4](
    vec2(-0.5, -0.5),
    vec2(-0.5,  0.5),
    vec2( 0.5,  0.5),
    vec2( 0.5, -0.5)
);
const vec2 kUVs[4] = vec2[4](
    vec2(0.0, 1.0),
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0)
);
const int kIndices[6] = int[6](0, 1, 2, 0, 2, 3);

void main()
{
    int cornerIndex = kIndices[gl_VertexIndex % 6];
    vec2 corner = kCorners[cornerIndex];
    outUV = kUVs[cornerIndex];

    uint agentIndex = uint(gl_InstanceIndex);
    uint baseIndex = agentIndex * 4u;

    vec4 posAndBrightness = agentBuffer.data[baseIndex + 0u];
    vec4 velAndScale       = agentBuffer.data[baseIndex + 1u];
    vec4 colorAndPersonality = agentBuffer.data[baseIndex + 3u];

    vec3 center = posAndBrightness.xyz;
    float brightness = posAndBrightness.w;
    float size = velAndScale.w;

    // Camera right/up from the view matrix's basis columns (standard billboard trick)
    vec3 cameraRight = vec3(frame.view[0][0], frame.view[1][0], frame.view[2][0]);
    vec3 cameraUp    = vec3(frame.view[0][1], frame.view[1][1], frame.view[2][1]);

    vec2 local = corner * size;
    vec3 worldPos = center + cameraRight * local.x + cameraUp * local.y;

    gl_Position = frame.proj * frame.view * vec4(worldPos, 1.0);

    vec3 fireflyColor = colorAndPersonality.xyz;
    outColor = vec4(fireflyColor * brightness, 1.0);
}
