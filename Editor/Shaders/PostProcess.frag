//------------------------------------------------------------------------------
// PostProcess.frag
//
// Simplified edge-aware anti-aliasing, not canonical FXAA (self-contained —
// this codebase has no shared .glsl include mechanism, see .claude/
// CLAUDE.md). Detect local luma contrast to find edges, box-blur (center +
// 4 neighbors) only those pixels, blended in by edge strength. A real
// directional single-sample variant was tried first and abandoned — it had
// a sign/orientation bug that made it replace a pixel with a neighbor's
// color instead of averaging them, producing no visible softening despite
// edge detection itself working correctly (confirmed via a debug overlay
// that painted detected edges red — they traced grass/terrain/cloud
// silhouettes correctly, so the bug was in the blend step, not detection).
// This cruder box-blur has no directionality to get wrong.
//
// Descriptor sets:
//   set 0 - scene-color texture (the scene pass's offscreen render target)
//------------------------------------------------------------------------------
#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;

layout(push_constant) uniform PushConstants
{
    int aaEnabled; // Debug Panel toggle — lets the on/off difference be
                   // compared live in the same view, since this effect is
                   // subtle in static screenshots.
} pc;

float Luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main()
{
    vec3 colorCenter = texture(sceneColor, inUV).rgb;

    if (pc.aaEnabled == 0)
    {
        outColor = vec4(colorCenter, 1.0);
        return;
    }

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

    // Skip low-contrast pixels entirely — not an edge, blending here would
    // just soften detail for no anti-aliasing benefit.
    const float kEdgeThresholdMin = 0.0312;
    const float kEdgeThresholdMax = 0.125;
    float threshold = max(kEdgeThresholdMin, lumaMax * kEdgeThresholdMax);
    if (lumaRange < threshold)
    {
        outColor = vec4(colorCenter, 1.0);
        return;
    }

    // Widened 9-tap box blur — radius kWideningFactor texels (not 1), and
    // including diagonals, not just N/S/E/W. The plain 5-tap version at a
    // 1-texel radius was mathematically a real blend (confirmed: edge
    // pixels' colors do change), but at typical screenshot/zoom scrutiny a
    // single softened pixel next to an otherwise-unchanged neighbor still
    // reads as "a chunky pixel step", not "a smooth edge". Widening to 2.5
    // texels/0.7 min blend made the effect clearly visible but noticeably
    // over-softened the whole image; these constants (1.4 / 0.35) are a
    // calmer middle point, not a carefully tuned final value — revisit if
    // it still looks too soft or too weak. See ROADMAP.md for the full
    // investigation history and open follow-ups (e.g. exposing these as
    // panel-tunable instead of hardcoded).
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

    // Once a pixel clears the edge threshold at all, blend it strongly
    // (at least 70%) rather than scaling all the way down to near-zero for
    // borderline-contrast edges — borderline edges are exactly the ones
    // that look "stepped" rather than "thin", so they need the blend too.
    float edgeStrength = clamp(lumaRange / max(lumaMax, 0.0001), 0.0, 1.0);
    float blendAmount = max(edgeStrength, 0.35);

    outColor = vec4(mix(colorCenter, blurred, blendAmount), 1.0);
}
