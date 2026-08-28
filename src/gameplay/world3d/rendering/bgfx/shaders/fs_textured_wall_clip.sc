$input v_color0, v_texcoord0, v_position0, v_normal0

#include "bgfx_shader.sh"

SAMPLER2D(s_texColor, 0);
uniform vec4 u_tintCutoff;
uniform vec4 u_colorAdjust;
uniform vec4 u_textureBlur;
uniform vec4 u_uvOffset;
uniform vec4 u_lightDir;
uniform vec4 u_lightParams;
uniform vec4 u_cameraClip;

void main()
{
    // Default room walls use an identity model transform, so v_position0 is
    // world space. Only this wall-specific program receives a non-zero radius.
    if (u_cameraClip.w > 0.001 && distance(v_position0, u_cameraClip.xyz) < u_cameraClip.w)
    {
        discard;
    }
    vec2 animatedUv = v_texcoord0 + u_uvOffset.xy;
    vec4 texel = texture2D(s_texColor, animatedUv);
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
