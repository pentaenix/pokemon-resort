$input v_color0, v_texcoord0, v_position0, v_normal0

#include "bgfx_shader.sh"

SAMPLER2D(s_texColor, 0);
uniform vec4 u_tintCutoff;
uniform vec4 u_colorAdjust;
uniform vec4 u_textureBlur;
uniform vec4 u_uvOffset;
uniform vec4 u_lightDir;
uniform vec4 u_lightParams;

void main()
{
    vec2 animatedUv = v_texcoord0 + u_uvOffset.xy;
    vec4 texel = texture2D(s_texColor, animatedUv);
    float blurAmount = 0.0;
    if (u_textureBlur.z > 0.001)
    {
        float depth = abs(v_position0.z - u_textureBlur.w);
        blurAmount = clamp((depth - u_colorAdjust.w) / max(u_textureBlur.z, 0.001), 0.0, 1.0);
        blurAmount = blurAmount * blurAmount * (3.0 - 2.0 * blurAmount);
    }
    if (blurAmount > 0.001)
    {
        vec2 stepUv = u_textureBlur.xy * blurAmount;
        vec2 halfUv = stepUv * 0.45;
        vec4 blurred = texel * 0.20;
        blurred += texture2D(s_texColor, animatedUv + vec2(halfUv.x, 0.0)) * 0.10;
        blurred += texture2D(s_texColor, animatedUv - vec2(halfUv.x, 0.0)) * 0.10;
        blurred += texture2D(s_texColor, animatedUv + vec2(0.0, halfUv.y)) * 0.10;
        blurred += texture2D(s_texColor, animatedUv - vec2(0.0, halfUv.y)) * 0.10;
        blurred += texture2D(s_texColor, animatedUv + vec2(stepUv.x, stepUv.y * 0.35)) * 0.07;
        blurred += texture2D(s_texColor, animatedUv - vec2(stepUv.x, stepUv.y * 0.35)) * 0.07;
        blurred += texture2D(s_texColor, animatedUv + vec2(stepUv.x * 0.35, stepUv.y)) * 0.07;
        blurred += texture2D(s_texColor, animatedUv - vec2(stepUv.x * 0.35, stepUv.y)) * 0.07;
        blurred += texture2D(s_texColor, animatedUv + vec2(stepUv.x * 0.75, -stepUv.y * 0.75)) * 0.04;
        blurred += texture2D(s_texColor, animatedUv + vec2(-stepUv.x * 0.75, stepUv.y * 0.75)) * 0.04;
        blurred += texture2D(s_texColor, animatedUv + vec2(stepUv.x * 0.75, stepUv.y * 0.75)) * 0.04;
        blurred += texture2D(s_texColor, animatedUv - vec2(stepUv.x * 0.75, stepUv.y * 0.75)) * 0.04;
        texel = blurred;
    }
    if (texel.a < u_tintCutoff.a)
    {
        discard;
    }
    vec4 color = texel * v_color0;
    float luma = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    color.rgb = mix(vec3(luma, luma, luma), color.rgb, u_colorAdjust.y);
    color.rgb = (color.rgb - vec3(0.5, 0.5, 0.5)) * u_colorAdjust.z + vec3(0.5, 0.5, 0.5);
    color.rgb *= u_colorAdjust.x;
    vec3 normal = normalize(v_normal0);
    vec3 lightDir = normalize(u_lightDir.xyz);
    float lit = max(dot(normal, lightDir), 0.0);
    vec3 formDir = normalize(vec3(0.40, -0.72, 0.42));
    float underSide = max(dot(normal, formDir), 0.0);
    float shade = u_lightParams.x + lit * u_lightParams.y;
    shade *= 1.0 - underSide * u_lightParams.z;
    shade = clamp(shade, 0.48, 1.25);
    color.rgb *= shade;
    gl_FragColor = color * vec4(u_tintCutoff.rgb, 1.0);
}
