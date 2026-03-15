//------------------------------------------------------------------------------
// Mesh.vert - CORRECTED VERSION
//
// Vertex shader for PBR mesh rendering
// IMPORTANT: Outputs MUST match Mesh.frag inputs exactly!
//------------------------------------------------------------------------------
#version 450

// ---- Descriptor Set 0: Frame Uniforms ----
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    vec4 time;
    vec4 cameraPos;
} frame;

// ---- Push Constants ----
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 customData;
} push;

// ---- Vertex Inputs ----
// Must match your VertexPCU/VertexPNT format in VulkanPipeline.cpp
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;      // Note: This is INPUT, different from output
layout(location = 2) in vec2 inTexCoord;

// ---- Vertex Outputs (to Fragment Shader) ----
// CRITICAL: These MUST match Mesh.frag inputs EXACTLY!
layout(location = 0) out vec3 fragNormal;      // vec3 - matches frag input
layout(location = 1) out vec2 fragTexCoord;    // vec2 - matches frag input (NOT vec3!)
layout(location = 2) out vec3 fragWorldPos;    // vec3 - matches frag input

void main() {
    // World position (needed for specular + point light distance)
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // World normal (normal matrix = transpose of inverse of upper-left 3x3)
    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    fragNormal = normalize(normalMatrix * inNormal);

    // Pass through texture coordinates (vec2, not vec3!)
    fragTexCoord = inTexCoord;

    // Final clip-space position
    gl_Position = frame.proj * frame.view * worldPos;
}
