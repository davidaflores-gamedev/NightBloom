//------------------------------------------------------------------------------
// Shadow.vert
//
// Vertex shader for shadow map rendering
// Outputs position in light space for depth writing
//------------------------------------------------------------------------------
#version 450

// Vertex input
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;     // Not used but must be declared
layout(location = 2) in vec2 inTexCoord;   // Not used but must be declared
// layout(location = 3) in vec4 inTangent;    // Not used but must be declared

// Uniform buffer - same as main rendering
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    vec4 time;
    vec4 cameraPos;
} frame;

// Push constants
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 customData;  // x,y,z,w can be used for light space matrix index, etc.
} push;

void main()
{
    // Transform vertex to clip space using light's view-projection
    // Note: For shadow pass, frame.view and frame.proj should be set to 
    // the light's view and projection matrices
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = frame.proj * frame.view * worldPos;
}
