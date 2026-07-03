//------------------------------------------------------------------------------
// PostProcess.frag
//
// Final composite of the HDR scene into the sRGB swapchain. In order:
//   1. FXAA-style edge-aware box blur (toggle) — see the AA notes below.
//   2. Additive bloom composite (the half-res blurred bright-pass result),
//      added in LINEAR HDR before tonemapping so highlights bleed naturally.
//   3. Exposure multiply -> ACES filmic tonemap (toggle; off = hard clamp).
//   4. Vignette.
// The scene target is linear HDR (B10G11R11), so samples here can exceed 1.0;
// the sRGB swapchain applies the display OETF on write (no manual gamma).
//
// AA note: a directional single-sample variant was tried first and abandoned
// (it relocated edges instead of averaging — no visible softening); this box
// blur has no directionality to get wrong. Ideally AA would run post-tonemap
// on LDR — flagged as a future refinement. See .claude/ROADMAP.md.
//
// Descriptor sets:
//   set 0 - scene-color texture (the scene pass's offscreen HDR render target)
//   set 1 - bloom result (half-res blurred bright-pass, linear HDR)
// Push constants: aaEnabled, tonemapEnabled, exposure, vignetteStrength, bloomIntensity.
//------------------------------------------------------------------------------
#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 1, binding = 0) uniform sampler2D bloomTex;  // half-res blurred bloom (linear HDR)

// Tone mapping / grading params. Must match PostProcessParams on the C++ side
// (Renderer::RecordPostProcessPass). Push-constant block = std430 scalar layout.
layout(push_constant) uniform PushConstants
{
    int   aaEnabled;        // FXAA edge-aware AA on/off
    int   tonemapEnabled;   // ACES filmic tonemap on/off (off = hard clamp to [0,1])
    float exposure;         // linear exposure multiplier applied before tonemap
    float vignetteStrength; // 0 = none; darkens toward frame edges
    float bloomIntensity;   // additive bloom strength (0 = off)
} pc;

float Luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// ACES filmic tone curve (Narkowicz approximation). Operates on linear HDR and
// returns linear [0,1]; the sRGB swapchain applies the display OETF on write, so
// no manual gamma here. Compresses highlights gracefully instead of clipping —
// the whole reason for the HDR scene target.
vec3 ACESFilmic(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    // The scene-color target is now LINEAR HDR (B10G11R11), so these samples can
    // exceed 1.0. FXAA runs here on HDR values (acceptable; ideally it would run
    // post-tonemap on LDR — flagged as a future refinement, AA is already a known
    // soft spot). The resolved color is then exposure-scaled, tonemapped, and
    // vignetted before being written to the sRGB swapchain.
    vec3 colorCenter = texture(sceneColor, inUV).rgb;
    vec3 sceneCol = colorCenter;

    if (pc.aaEnabled != 0)
    {
    vec2 texel = 1.0 / vec2(textureSize(sceneColor, 0));

    vec3 colorUp    = texture(sceneColor, inUV + vec2(0.0, texel.y)).rgb;
    vec3 colorDown  = texture(sceneColor, inUV - vec2(0.0, texel.y)).rgb;
    vec3 colorLeft  = texture(sceneColor, inUV - vec2(texel.x, 0.0)).rgb;
    vec3 colorRight = texture(sceneColor, inUV + vec2(texel.x, 0.0)).rgb;

    float lumaCenter = Luma(colorCenter);
    float lumaUp = Luma(colorUp);
    float lumaDown = Luma(colorDown);
    float lumaLeft = Luma(colorLeft);
    float lumaRight = Luma(colorRight);

    float lumaMin = min(lumaCenter, min(min(lumaUp, lumaDown), min(lumaLeft, lumaRight)));
    float lumaMax = max(lumaCenter, max(max(lumaUp, lumaDown), max(lumaLeft, lumaRight)));
    float lumaRange = lumaMax - lumaMin;

    // Only blur actual edges; low-contrast interiors pass through unchanged.
    const float kEdgeThresholdMin = 0.0312;
    const float kEdgeThresholdMax = 0.125;
    float threshold = max(kEdgeThresholdMin, lumaMax * kEdgeThresholdMax);
    if (lumaRange >= threshold)
    {
        // Widened 9-tap box blur — radius kWideningFactor texels, including
        // diagonals. See ROADMAP.md for the full FXAA investigation history;
        // these constants (1.4 / 0.35) are a calm middle point, not final.
        const float kWideningFactor = 1.4;
        vec2 wideTexel = texel * kWideningFactor;

        vec3 colorUpLeft    = texture(sceneColor, inUV + vec2(-wideTexel.x,  wideTexel.y)).rgb;
        vec3 colorUpRight   = texture(sceneColor, inUV + vec2( wideTexel.x,  wideTexel.y)).rgb;
        vec3 colorDownLeft  = texture(sceneColor, inUV + vec2(-wideTexel.x, -wideTexel.y)).rgb;
        vec3 colorDownRight = texture(sceneColor, inUV + vec2( wideTexel.x, -wideTexel.y)).rgb;
        vec3 colorWideUp    = texture(sceneColor, inUV + vec2(0.0,  wideTexel.y)).rgb;
        vec3 colorWideDown  = texture(sceneColor, inUV + vec2(0.0, -wideTexel.y)).rgb;
        vec3 colorWideLeft  = texture(sceneColor, inUV + vec2(-wideTexel.x, 0.0)).rgb;
        vec3 colorWideRight = texture(sceneColor, inUV + vec2( wideTexel.x, 0.0)).rgb;

        vec3 blurred = (colorCenter + colorWideUp + colorWideDown + colorWideLeft + colorWideRight
            + colorUpLeft + colorUpRight + colorDownLeft + colorDownRight) / 9.0;

        float edgeStrength = clamp(lumaRange / max(lumaMax, 0.0001), 0.0, 1.0);
        float blendAmount = max(edgeStrength, 0.35);
        sceneCol = mix(colorCenter, blurred, blendAmount);
    }
    } // end FXAA

    // ---- Bloom composite (additive, in linear HDR before tonemap) ----
    // bloomTex is half-res; the linear sampler upscales it. Adding in HDR (not after
    // tonemap) is what gives bloom that natural "bleed into highlights" look.
    if (pc.bloomIntensity > 0.0)
        sceneCol += texture(bloomTex, inUV).rgb * pc.bloomIntensity;

    // ---- Tone / color grade ----
    // Exposure in linear HDR, then compress to displayable range. With tonemap
    // off we just clamp (so HDR still shows *something* sane on the 8-bit output).
    sceneCol *= pc.exposure;
    sceneCol = (pc.tonemapEnabled != 0) ? ACESFilmic(sceneCol) : clamp(sceneCol, 0.0, 1.0);

    // Vignette — gentle radial darkening toward the frame edge (display space).
    if (pc.vignetteStrength > 0.0)
    {
        float dist = length(inUV - vec2(0.5));
        float vig  = 1.0 - pc.vignetteStrength * smoothstep(0.35, 0.85, dist);
        sceneCol *= vig;
    }

    outColor = vec4(sceneCol, 1.0);
}
