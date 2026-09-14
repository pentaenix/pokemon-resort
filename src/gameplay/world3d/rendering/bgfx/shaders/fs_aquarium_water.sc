$input v_color0, v_texcoord0, v_position0, v_normal0

#include "bgfx_shader.sh"

SAMPLER2D(s_texColor, 0);
uniform vec4 u_tintCutoff;
uniform vec4 u_uvOffset;
uniform vec4 u_lightDir;
uniform vec4 u_lightParams;
// Local water bottom, local water surface, longest horizontal span, bounded-fog intensity.
uniform vec4 u_aquariumWaterBounds;

void main()
{
    // The opaque Black 2 ocean base is intentionally absent. This is only its
    // translucent moving highlight plane, sampled in opposing directions after
    // bounded tank fog. Combine in premultiplied space, then return straight
    // alpha for the renderer's ordinary source-alpha blend state.
    vec4 primary = texture2D(s_texColor, v_texcoord0 + u_uvOffset.xy);
    vec4 secondary = texture2D(s_texColor, v_texcoord0 - u_uvOffset.xy);
    float primaryAlpha = primary.a * v_color0.a * 0.50;
    float secondaryAlpha = secondary.a * v_color0.a * 0.275;
    float combinedAlpha = primaryAlpha + secondaryAlpha * (1.0 - primaryAlpha);
    vec3 combinedPremultiplied = primary.rgb * v_color0.rgb * primaryAlpha +
        secondary.rgb * v_color0.rgb * secondaryAlpha * (1.0 - primaryAlpha);
    vec4 color = vec4(
        combinedPremultiplied / max(combinedAlpha, 0.0001),
        combinedAlpha);

    vec3 normal = normalize(v_normal0);
    vec3 lightDir = normalize(u_lightDir.xyz);
    float lit = max(dot(normal, lightDir), 0.0);
    float shade = clamp(u_lightParams.x + lit * u_lightParams.y, 0.60, 1.20);
    color.rgb *= shade * u_tintCutoff.rgb;

    // Fog has already graded the scene below this pass. Dense water softens
    // this surface detail slightly instead of making a second opaque blue veil.
    float fogInfluence = 1.0 - exp(-max(0.0, u_aquariumWaterBounds.w - 1.0) * 0.10);
    float luma = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    color.rgb = mix(color.rgb, vec3(luma) * u_tintCutoff.rgb, fogInfluence * 0.20);
    color.a *= mix(1.0, 0.72, fogInfluence);
    gl_FragColor = color;
}
