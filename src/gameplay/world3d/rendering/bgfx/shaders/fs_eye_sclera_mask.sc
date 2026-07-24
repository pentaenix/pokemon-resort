$input v_color0, v_texcoord0, v_position0, v_normal0

#include "bgfx_shader.sh"

SAMPLER2D(s_texColor, 0);
uniform vec4 u_tintCutoff;

void main()
{
    vec4 texel = texture2D(s_texColor, v_texcoord0);
    // Resort supplies a generated socket-aperture texture here. Socket colors
    // vary by Pokemon, so its binary alpha is the complete stencil contract.
    if (texel.a < 0.01)
    {
        discard;
    }
    gl_FragColor = vec4(1.0);
}
