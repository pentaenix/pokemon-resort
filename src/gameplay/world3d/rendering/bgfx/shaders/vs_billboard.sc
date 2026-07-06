$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0, v_position0, v_normal0

#include "bgfx_shader.sh"

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
    v_position0 = a_position;
    v_normal0 = vec3(0.0, 1.0, 0.0);
}
