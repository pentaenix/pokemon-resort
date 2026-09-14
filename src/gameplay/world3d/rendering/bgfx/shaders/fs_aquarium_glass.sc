$input v_color0, v_texcoord0, v_position0, v_normal0

#include "bgfx_shader.sh"

uniform vec4 u_tintCutoff;
// Camera in tank-local space; reflection moves only when the view changes.
uniform vec4 u_aquariumWaterCamera;

void main()
{
    vec3 normal = normalize(v_normal0);
    vec3 viewDirection = normalize(u_aquariumWaterCamera.xyz - v_position0);
    float grazing = 1.0 - abs(dot(normal, viewDirection));
    float fresnel = grazing * grazing;

    // An analytic, broad room reflection requires no environment texture.
    // There are no positional bands or time terms: a stationary view stays
    // stationary, and curved panes reveal the reflection through their normals.
    vec3 reflectedView = reflect(-viewDirection, normal);
    // Use a horizontal room-light direction: from the elevated game camera,
    // vertical panes reflect the room below eye level, not the ceiling. Let
    // this broad lobe remain visible face-on instead of multiplying it away
    // with the grazing response.
    vec3 roomLightDirection = vec3(-0.8, 0.0, 0.6);
    float roomReflection = smoothstep(-0.70, 0.80, dot(reflectedView, roomLightDirection));
    float softReflection = 0.30 + 0.70 * roomReflection;
    float alpha = 0.035 + fresnel * 0.09 + softReflection * 0.055;

    // Neutral silver-blue retains the room's intensity without inheriting a
    // yellow/green light tint that can make highlights resemble editor marks.
    float roomIntensity = clamp(
        dot(u_tintCutoff.rgb, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
    vec3 glassTint = mix(vec3(0.72, 0.79, 0.84), vec3(0.90, 0.95, 0.98), softReflection);
    gl_FragColor = vec4(glassTint * roomIntensity, alpha);
}
