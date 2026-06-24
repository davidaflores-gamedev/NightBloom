//------------------------------------------------------------------------------
// Clouds.frag
//
// Thin composite shader — samples the low-res cloud raymarch result
// (computed by CloudRaymarch.comp, see .claude/ROADMAP.md Phase 1.4 for the
// performance rationale) with hardware bilinear upscale, and outputs it
// directly. No raymarching, no camera/lighting data needed here at all —
// occlusion against terrain/opaque geometry is still handled entirely by
// the normal full-resolution GPU depth test (Clouds.vert's fixed "at
// infinity" depth output, depth-test on, write off), unchanged from before
// this pass was split into compute + composite.
//
// Descriptor sets:
//   set 0 - the low-res raymarch result (only set needed)
//------------------------------------------------------------------------------
#version 450

layout(location = 0) in vec2 inNDC;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D cloudResultSampler;

void main()
{
    vec2 uv = inNDC * 0.5 + 0.5;
    outColor = texture(cloudResultSampler, uv);
}
