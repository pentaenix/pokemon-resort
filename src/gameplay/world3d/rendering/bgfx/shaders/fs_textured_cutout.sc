$input v_color0, v_texcoord0

#include "bgfx_shader.sh"

SAMPLER2D(s_texColor, 0);
uniform vec4 u_tintCutoff;

void main()
{
    vec4 texel = texture2D(s_texColor, v_texcoord0);
    if (texel.a < u_tintCutoff.a)
    {
        discard;
    }
    gl_FragColor = texel * v_color0 * vec4(u_tintCutoff.rgb, 1.0);
}
