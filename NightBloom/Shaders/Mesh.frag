//------------------------------------------------------------------------------
// Mesh.frag
//
// Opaque / glass mesh shading + cascaded shadows. Frame/lighting/shadow
// descriptor blocks and all CSM logic come from the shared includes
// (Shaders/Include/scene_common.glsl + shadows.glsl).
//------------------------------------------------------------------------------
#version 450

#include "shadows.glsl"   // set 0 frame, set 2 lighting, set 3 shadowMap + CSM functions

// ---- Descriptor Set 1: Albedo texture ----
layout(set = 1, binding = 0) uniform sampler2D texSampler;

// ---- Push Constants ----
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 customData;   // w > 0.01 => material-driven color (glass); xyz=tint, w=alpha
} push;

// ---- Fragment Inputs ----
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;

// ---- Output ----
layout(location = 0) out vec4 outColor;

// ============================================================================
// Cheap 3D value-noise FBM for the moon's surface mottling (maria/craters).
// Sampled on the sphere's object-space direction (== fragNormal, since the moon
// transform is translate + uniform-scale with no rotation), which is SEAMLESS —
// no UV seam or pole pinching like a fragTexCoord sample would show, and stays
// fixed to the moon surface as the moon translates.
// ============================================================================
float EmissiveHash3(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float EmissiveNoise3(vec3 x)
{
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(EmissiveHash3(i + vec3(0,0,0)), EmissiveHash3(i + vec3(1,0,0)), f.x),
                   mix(EmissiveHash3(i + vec3(0,1,0)), EmissiveHash3(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(EmissiveHash3(i + vec3(0,0,1)), EmissiveHash3(i + vec3(1,0,1)), f.x),
                   mix(EmissiveHash3(i + vec3(0,1,1)), EmissiveHash3(i + vec3(1,1,1)), f.x), f.y), f.z);
}

float EmissiveFbm3(vec3 p)
{
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        sum += amp * EmissiveNoise3(p);
        p *= 2.0;
        amp *= 0.5;
    }
    return sum;
}

// ============================================================================
// Blinn-Phong lighting
// ============================================================================
vec3 CalcBlinnPhong(vec3 N, vec3 V, vec3 lightDir, vec3 lightColor, float lightIntensity)
{
    float NdotL = max(dot(N, lightDir), 0.0);
    vec3 diffuse = lightColor * lightIntensity * NdotL;

    vec3 H = normalize(lightDir + V);
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, 32.0);
    vec3 specular = lightColor * lightIntensity * spec * 0.5;

    return diffuse + specular;
}

// ============================================================================
// Main
// ============================================================================
void main()
{
    ClipReflection(fragWorldPos);  // drop below-water geometry in the reflection pass

    vec4 texColor = texture(texSampler, fragTexCoord);

    // ---- Emissive branch (e.g. the moon) -------------------------------------
    // customData.w >= 2.0 flags an unlit emissive surface (glass uses w in
    // (0.01, 1.0] as alpha, so >= 2.0 is unambiguous; this MUST be checked before
    // the glass branch below). customData.rgb is the HDR emissive color (tint *
    // intensity), kept bright (> 1.0) so the HDR target + bloom bright-pass pick
    // it up. The day/night cycle drives rgb (warm/bright by day, cool/dim by
    // night); the bound texture (set 1) supplies surface detail.
    if (push.customData.w >= 2.0)
    {
        vec3 N = normalize(fragNormal);
        vec3 V = normalize(frame.cameraPos.xyz - fragWorldPos);
        float ndv = clamp(dot(N, V), 0.0, 1.0);
        vec3 emissive = texColor.rgb * push.customData.rgb;

        if (push.customData.w >= 2.5)
        {
            // SUN: flat, uniformly bright disc with a slightly hotter core; no limb
            // darkening, so it reads as a blazing light source (bloom does the glow).
            float core = mix(0.9, 1.3, ndv);
            outColor = vec4(emissive * core, 1.0);
        }
        else
        {
            // MOON: nearly-flat disc (only faint limb darkening so it doesn't read as
            // a shaded 3D ball) with subtle seamless maria/crater mottling. Kept dim
            // by the day/night intensity so its cool color + surface actually show
            // instead of blooming out to the same white blob as the sun.
            float limb = mix(0.8, 1.0, ndv);
            float mott = mix(0.45, 1.0, EmissiveFbm3(N * 3.0)); // larger, more visible maria
            outColor = vec4(emissive * limb * mott, 1.0);
        }
        return;
    }


    bool isMaterialDriven = (push.customData.w > 0.01);
    vec4 albedo = isMaterialDriven ? push.customData : texColor;

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(frame.cameraPos.xyz - fragWorldPos);

    vec3 totalLight = lighting.ambient.rgb * lighting.ambient.a;

    // Cascaded shadow factor for the primary directional light (lights[0]).
    int cascade;
    vec3 sunDir = normalize(-lighting.lights[0].position.xyz);
    float shadowFactor = SampleShadow(fragWorldPos, N, sunDir, 1.0, 0.005, cascade);

    for (int i = 0; i < lighting.numLights; ++i)
    {
        LightData light = lighting.lights[i];
        float lightType = light.position.w;

        vec3 lightDir;
        float attenuation = 1.0;
        float lightShadow = 1.0;

        if (lightType < 0.5)
        {
            lightDir = normalize(-light.position.xyz);
            if (i == 0) lightShadow = shadowFactor;
        }
        else
        {
            vec3 toLight = light.position.xyz - fragWorldPos;
            float dist = length(toLight);
            lightDir = toLight / max(dist, 0.0001);

            float radius = light.attenuation.w;
            if (dist > radius) continue;

            float c = light.attenuation.x;
            float l = light.attenuation.y;
            float q = light.attenuation.z;
            attenuation = 1.0 / (c + l * dist + q * dist * dist);
            attenuation *= 1.0 - smoothstep(radius * 0.75, radius, dist);
            lightShadow = 1.0;
        }

        vec3 contribution = CalcBlinnPhong(N, V, lightDir, light.color.rgb, light.color.a);
        totalLight += contribution * attenuation * lightShadow;
    }

    if (isMaterialDriven)
    {
        float NdotV = clamp(dot(N, V), 0.0, 1.0);
        float fresnel = pow(1.0 - NdotV, 5.0);
        vec3 tint = albedo.rgb;
        vec3 reflectionColor = vec3(1.0);
        float reflectStrength = 0.15 + 0.85 * fresnel;
        vec3 glassRgb = mix(tint * totalLight, reflectionColor, reflectStrength);
        outColor = vec4(ApplyCascadeDebug(glassRgb, cascade), albedo.a);
        return;
    }

    outColor = vec4(ApplyCascadeDebug(albedo.rgb * totalLight, cascade), albedo.a);
}
