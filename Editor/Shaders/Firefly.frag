//------------------------------------------------------------------------------
// Firefly.frag
//
// Radial glow falloff for a firefly billboard, additive-alpha blended.
//------------------------------------------------------------------------------
#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
    vec2 p = inUV * 2.0 - 1.0;
    float dist = length(p);

    if (dist > 1.0) discard;

    float glow = clamp(1.0 - dist, 0.0, 1.0);
    glow *= glow;
    glow *= glow;

    vec3 color = inColor.rgb * (0.25 + 2.5 * glow);
    float alpha = glow;

    outColor = vec4(color, alpha);
}
