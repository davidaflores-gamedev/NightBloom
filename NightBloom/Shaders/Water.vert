//------------------------------------------------------------------------------
// Water.vert
//
// Transforms the flat water plane (a VertexPNT grid generated at Y=0, the same
// generator the terrain uses). The push-constant model matrix lifts it to the
// water surface height. No vertex displacement — the "waves" are an animated
// normal perturbation in Water.frag, so this is a plain transform.
//------------------------------------------------------------------------------
#version 450

// ---- Set 0: Frame Uniforms (scene camera) ----
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    vec4 time;
    vec4 cameraPos;
} frame;

// ---- Push Constants ----
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 customData; // (waveAmplitude, waveSpeed, fresnelPower, alpha)
} push;

// ---- Vertex Inputs (VertexPNT) ----
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// ---- Outputs ----
layout(location = 0) out vec3 fragWorldPos;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    gl_Position = frame.proj * frame.view * worldPos;
}
