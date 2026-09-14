$input v_color0, v_texcoord0, v_position0, v_normal0
#include "bgfx_shader.sh"
SAMPLER2D(s_texColor, 0);
uniform vec4 u_tintCutoff;
uniform vec4 u_colorAdjust;
uniform vec4 u_pulseBounds;
uniform vec4 u_pulseState;
void main()
{
    float radius = length(v_position0 - u_pulseBounds.xyz) / max(u_pulseBounds.w, 0.001);
    float front = smoothstep(radius - 0.07, radius + 0.07, u_pulseState.x);
    float glow = mix(front, 1.0 - front, u_pulseState.y);
    vec4 texel = texture2D(s_texColor, v_texcoord0);
    gl_FragColor = texel * v_color0 * vec4(u_tintCutoff.rgb * u_colorAdjust.x * glow, 1.0);
}
