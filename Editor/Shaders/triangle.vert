#version 450
#extension GL_ARB_separate_shader_objects : enable

// Input from vertex buffer
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

// Output to fragment shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragUV;

void main() {
    gl_Position = vec4(inPosition, 1.0);  // Just pass through position for now
    fragColor = inColor;  // Pass color to fragment shader
    fragUV = inUV;        // Pass UV to fragment shader (unused for now)
}