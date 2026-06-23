// Terrain Shadow vert
// this is basically a shader that needs to be bound when drawing the shadow shader because unfortuantely shits fucked XD

#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform FrameUBO
{
    mat4 view;
    mat4 proj;
    vec4 time;
    vec4 cameraPos;
} frame;

layout(set = 1, binding = 0) uniform sampler2D heightmap;

layout(push_constant) uniform PushConstants
{
    mat4 model;
    vec4 customData; // x = heightScale, y = texelSize
} pc;

void main()
{
    float h = textureLod(heightmap, inTexCoord, 0.0).r * pc.customData.x;
    vec3 displacedPos = inPosition + vec3(0.0, h, 0.0);
    gl_Position = frame.proj * frame.view * pc.model * vec4(displacedPos, 1.0);
}