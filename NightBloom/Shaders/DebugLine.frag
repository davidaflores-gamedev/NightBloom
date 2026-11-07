//------------------------------------------------------------------------------
// DebugLine.frag — flat colour. Written into the HDR scene target, so values
// above 1.0 bloom.
//------------------------------------------------------------------------------
#version 450

layout(location = 0) in vec3 inColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(inColor, 1.0);
}
