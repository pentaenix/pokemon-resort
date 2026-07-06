$input v_color0, v_texcoord0, v_position0, v_normal0

#include "bgfx_shader.sh"

SAMPLER2D(s_texColor, 0);
uniform vec4 u_tintCutoff;

void main()
{
    vec4 texel = texture2D(s_texColor, v_texcoord0);
    float hi = max(texel.r, max(texel.g, texel.b));
    float lo = min(texel.r, min(texel.g, texel.b));
    float chroma = hi - lo;
    float luminance = dot(texel.rgb, vec3(0.299, 0.587, 0.114));
    if (texel.a < 0.01 || chroma > 0.18 || luminance < 0.42)
    {
        discard;
    }
    gl_FragColor = vec4(1.0);
}
