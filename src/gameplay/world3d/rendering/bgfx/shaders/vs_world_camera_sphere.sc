$input a_position, a_normal, a_color0, a_texcoord0
$output v_color0, v_texcoord0, v_position0, v_normal0

#include "bgfx_shader.sh"

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_color0 = a_color0;
    vec3 cameraNormal = normalize(mul(u_modelView, vec4(a_normal, 0.0)).xyz);
    v_texcoord0 = cameraNormal.xy * 0.5 + vec2(0.5, 0.5);
    v_position0 = a_position;
    v_normal0 = normalize(a_normal);
}
