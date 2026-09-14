$input v_position0

#include "bgfx_shader.sh"

SAMPLER2D(s_aquariumSceneDepth, 0);

uniform mat4 u_aquariumFogInvViewProj;
uniform vec4 u_aquariumFogCamera;
uniform vec4 u_aquariumFogBoxMin;
uniform vec4 u_aquariumFogBoxMax;
// Clear distance, visibility/far distance, falloff gamma, maximum opacity.
uniform vec4 u_aquariumFogParams;
// RGB fog color; W is one when projection depth is homogeneous [-1, 1].
uniform vec4 u_aquariumFogColor;

bool clipFogSlab(
    float origin,
    float direction,
    float slabMin,
    float slabMax,
    inout float nearDistance,
    inout float farDistance)
{
    if (abs(direction) < 0.0000001)
    {
        return origin >= slabMin && origin <= slabMax;
    }
    float first = (slabMin - origin) / direction;
    float second = (slabMax - origin) / direction;
    float axisNear = min(first, second);
    float axisFar = max(first, second);
    nearDistance = max(nearDistance, axisNear);
    farDistance = min(farDistance, axisFar);
    return nearDistance <= farDistance;
}

void main()
{
    vec2 uv = gl_FragCoord.xy * u_viewTexel.xy;
    float deviceDepth = texture2D(s_aquariumSceneDepth, uv).x;

    vec3 camera = u_aquariumFogCamera.xyz;
    vec3 rayDirection = normalize(v_position0 - camera);
    float nearDistance = -1000000.0;
    float farDistance = 1000000.0;
    bool hit = clipFogSlab(
        camera.x, rayDirection.x, u_aquariumFogBoxMin.x,
        u_aquariumFogBoxMax.x, nearDistance, farDistance);
    hit = hit && clipFogSlab(
        camera.y, rayDirection.y, u_aquariumFogBoxMin.y,
        u_aquariumFogBoxMax.y, nearDistance, farDistance);
    hit = hit && clipFogSlab(
        camera.z, rayDirection.z, u_aquariumFogBoxMin.z,
        u_aquariumFogBoxMax.z, nearDistance, farDistance);
    if (!hit)
    {
        discard;
    }

    float surfaceDistance = 1000000.0;
    bool hasOpaqueSurface = deviceDepth < 0.999999;
    // This renderer uses conventional (non-reversed) depth with a clear value
    // of one. Keep background pixels at infinity so the complete tank chord is
    // fogged when the aquarium is viewed against the clear background.
    if (hasOpaqueSurface)
    {
        float clipDepth = mix(
            deviceDepth,
            deviceDepth * 2.0 - 1.0,
            u_aquariumFogColor.w);
        vec3 clip = vec3(uv * 2.0 - 1.0, clipDepth);
#if !BGFX_SHADER_LANGUAGE_GLSL
        clip.y = -clip.y;
#endif
        vec4 worldH = mul(u_aquariumFogInvViewProj, vec4(clip, 1.0));
        vec3 surfaceWorld = worldH.xyz / max(abs(worldH.w), 0.000001) * sign(worldH.w);
        surfaceDistance = max(0.0, dot(surfaceWorld - camera, rayDirection));
    }

    // The fog proxy itself deliberately has no hardware depth test so front
    // water can grade opaque fish and substrate behind it. Reject only proxy
    // fragments that are farther away than the sampled opaque surface. This
    // prevents below-floor tank sides from painting over the room floor while
    // preserving top/front water coverage.
    float proxyDistance = length(v_position0 - camera);
    if (hasOpaqueSurface && proxyDistance > surfaceDistance + 0.5)
    {
        discard;
    }

    float waterStart = max(nearDistance, 0.0);
    float waterEnd = min(farDistance, surfaceDistance);
    float waterPath = max(0.0, waterEnd - waterStart);
    if (waterPath <= 0.000001)
    {
        discard;
    }
    float span = max(
        u_aquariumFogParams.y - u_aquariumFogParams.x,
        0.000001);
    float opticalDistance = max(
        0.0,
        (waterPath - u_aquariumFogParams.x) / span);
    float extinction = 1.0 - exp(-pow(
        opticalDistance,
        max(u_aquariumFogParams.z, 0.05)));
    float coverage = clamp(
        u_aquariumFogParams.w * extinction,
        0.0,
        1.0);
    // Absorption removes energy as well as contrast. This slower curve remains
    // visibly progressive after haze is strong, avoiding a flat-colored core.
    float distanceDarkening = 1.0 - exp(-opticalDistance * 0.22);
    vec3 fogTarget = u_aquariumFogColor.rgb * mix(
        1.0, 0.55, distanceDarkening);
    // Absorption follows the actual glass-to-visible-surface water segment.
    // This makes sand near the viewing glass stay light while the same sand
    // seen through more water darkens progressively. Encode absorption and
    // haze as one premultiplied-alpha operator so overlapping tank silhouettes
    // compose with the existing fog target rather than restoring the original
    // scene sampled before any tank pass.
    float pathTransmittance = exp(-opticalDistance * 0.22);
    float compositeOpacity = clamp(
        1.0 - pathTransmittance * (1.0 - coverage),
        0.0,
        1.0);
    gl_FragColor = vec4(fogTarget * coverage, compositeOpacity);
}
