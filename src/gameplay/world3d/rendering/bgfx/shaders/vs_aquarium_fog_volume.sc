$input a_position
$output v_position0

#include "bgfx_shader.sh"

void main()
{
    vec4 world = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, world);
    v_position0 = world.xyz;
}
