//------------------------------------------------------------------------------
// Mesh.frag - WITH DEBUG VISUALIZATION
//
// Uncomment the debug sections to visualize what's happening
//------------------------------------------------------------------------------
#version 450

// ---- Descriptor Set 0: Frame Uniforms ----
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    vec4 time;
    vec4 cameraPos;
} frame;

// ---- Descriptor Set 1: Textures ----
layout(set = 1, binding = 0) uniform sampler2D texSampler;

// ---- Descriptor Set 2: Scene Lighting ----
struct LightData {
    vec4 position;
    vec4 color;
    vec4 attenuation;
};

struct ShadowData {
    mat4 lightSpaceMatrix;
    vec4 shadowParams;
};

layout(std140, set = 2, binding = 0) uniform SceneLighting {
    LightData lights[16];
    vec4 ambient;
    int numLights;
    int _pad1, _pad2, _pad3;
    ShadowData shadowData;
} lighting;

// ---- Descriptor Set 3: Shadow Map ----
layout(set = 3, binding = 0) uniform sampler2DShadow shadowMap;

// ---- Push Constants ----
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 customData;
} push;

// ---- Fragment Inputs ----
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;

// ---- Output ----
layout(location = 0) out vec4 outColor;

// ============================================================================
// Shadow calculation with debug options
// ============================================================================
float CalculateShadow(vec3 worldPos, vec3 normal)
{
    // Check if shadows are enabled
    if (lighting.shadowData.shadowParams.w < 0.5)
        return 1.0;
    
    // Transform world position to light space
    vec4 lightSpacePos = lighting.shadowData.lightSpaceMatrix * vec4(worldPos, 1.0);
    
    // Perspective divide
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    
    // Transform from [-1,1] to [0,1] for texture sampling
    // Note: If Y-flip is applied in the projection matrix, we don't need to flip here
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
    // =========================================================================
    // ALTERNATIVE: If shadows are upside-down, try flipping Y here instead
    // Uncomment this line if you're NOT applying Y-flip in the projection matrix:
    // projCoords.y = 1.0 - projCoords.y;
    // =========================================================================
    
    // Check if fragment is outside the shadow map frustum
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0)
    {
        return 1.0;  // Outside shadow frustum = fully lit
    }
    
    // Apply bias
    float bias = lighting.shadowData.shadowParams.x;
    float currentDepth = projCoords.z - bias;
    
    // Sample shadow map with hardware PCF
    float shadow = texture(shadowMap, vec3(projCoords.xy, currentDepth));
    
    return shadow;
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
    vec4 texColor = texture(texSampler, fragTexCoord);
    bool isMaterialDriven = (push.customData.w > 0.01);
    vec4 albedo = isMaterialDriven ? push.customData : texColor;

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(frame.cameraPos.xyz - fragWorldPos);

    // =========================================================================
    // DEBUG MODE 1: Visualize shadow factor only
    // Uncomment to see: white = lit, black = shadow
    // =========================================================================
    
    //float shadowDebug = CalculateShadow(fragWorldPos, N);
    //outColor = vec4(vec3(shadowDebug), 1.0);
    //return;
    

    // =========================================================================
    // DEBUG MODE 2: Visualize light-space UV coordinates
    // Uncomment to see the shadow map UV mapping (red=U, green=V)
    // =========================================================================
    /*
    vec4 lightSpacePos = lighting.shadowData.lightSpaceMatrix * vec4(fragWorldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;
    outColor = vec4(projCoords.xy, 0.0, 1.0);
    return;
    */

    // =========================================================================
    // DEBUG MODE 3: Visualize light-space depth
    // Uncomment to see depth gradient (black = near, white = far)
    // =========================================================================
    /*
    vec4 lightSpacePos = lighting.shadowData.lightSpaceMatrix * vec4(fragWorldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;
    outColor = vec4(vec3(projCoords.z), 1.0);
    return;
    */

    // Normal rendering with shadows
    vec3 totalLight = lighting.ambient.rgb * lighting.ambient.a;
    float shadowFactor = CalculateShadow(fragWorldPos, N);

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
        outColor = vec4(glassRgb, albedo.a);
        return;
    }

    outColor = vec4(albedo.rgb * totalLight, albedo.a);
}
