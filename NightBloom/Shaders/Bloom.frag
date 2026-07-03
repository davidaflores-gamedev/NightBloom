//------------------------------------------------------------------------------
// Bloom.frag
//
// One shader for the whole bloom chain, switched by a push-constant mode. Runs
// as a full-screen triangle (PostProcess.vert) into the half-res HDR bloom
// targets. See Renderer::RecordBloomPass.
//
//   mode 0 (extract): sample the HDR scene color, soft-threshold the bright part.
//                     The target is half-res, so the bilinear fetch already
//                     downsamples a 2x2 block.
//   mode 1 (blur):    separable 9-tap Gaussian along `direction` (in texels).
//                     Run once horizontally (A->B) then once vertically (B->A).
//
//   set 0, binding 0 -> sampler2D inputTex (scene color, or a bloom ping-pong target)
//------------------------------------------------------------------------------
#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTex;

layout(push_constant) uniform PushConstants
{
    vec2  direction;  // blur step direction in texel multiples (blur mode); unused in extract
    float threshold;  // bright-pass luma threshold (extract mode)
    int   mode;       // 0 = bright extract+downsample, 1 = separable blur
} pc;

float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(inputTex, 0));

    if (pc.mode == 0)
    {
        // Bright extract with a soft knee so bright pixels ramp in smoothly rather
        // than popping at a hard cutoff. Weight by how far luma is past threshold.
        vec3  c = texture(inputTex, inUV).rgb;
        float l = Luma(c);
        const float knee = 0.5;
        float w = clamp((l - pc.threshold) / knee, 0.0, 1.0);
        outColor = vec4(c * w, 1.0);
        return;
    }

    // Separable Gaussian. Weights for a ~sigma-2 kernel; linear filtering between
    // texels softens the gaps if `direction` is scaled past 1.
    const float kW[5] = float[5](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 stepv = pc.direction * texel;

    vec3 sum = texture(inputTex, inUV).rgb * kW[0];
    for (int i = 1; i < 5; ++i)
    {
        sum += texture(inputTex, inUV + stepv * float(i)).rgb * kW[i];
        sum += texture(inputTex, inUV - stepv * float(i)).rgb * kW[i];
    }
    outColor = vec4(sum, 1.0);
}
