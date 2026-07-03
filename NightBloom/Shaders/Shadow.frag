//------------------------------------------------------------------------------
// Shadow.frag
//
// Fragment shader for shadow map rendering
// Minimal shader - just writes depth (no color output)
//
// Note: For opaque geometry, you could technically skip the fragment shader
// entirely, but having one allows for alpha-tested shadows later.
//------------------------------------------------------------------------------
#version 450

// No output - depth-only render pass
// The depth is automatically written by the fixed-function depth test

void main()
{
    // Empty - depth is written automatically
    // 
    // Future: Add alpha testing here for transparent shadows
    // if (texture(albedoMap, inTexCoord).a < 0.5)
    //     discard;
}
