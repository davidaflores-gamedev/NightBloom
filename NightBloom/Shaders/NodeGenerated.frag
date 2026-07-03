#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    float time;
} push;

void main() {
    float time = push.time;

    // Output
    outColor = vec4(1.0, 0.0, 1.0, 1.0);
}
