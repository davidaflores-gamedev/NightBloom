#version 450
#extension GL_ARB_separate_shader_objects : enable

// Input from vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragUV;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // For barycentric triangle, just output interpolated vertex colors
    outColor = vec4(fragColor, 1.0);
    
    // Later you could use UV for texturing:
    // outColor = texture(texSampler, fragUV);
}