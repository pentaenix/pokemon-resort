$input v_color0, v_texcoord0, v_texcoord1, v_texcoord2, v_position0, v_normal0

#include "bgfx_shader.sh"

SAMPLER2D(s_picaTex0, 0);
SAMPLER2D(s_picaTex1, 1);
SAMPLER2D(s_picaTex2, 2);
uniform vec4 u_tintCutoff;
uniform vec4 u_picaUvOffsets[3];
uniform vec4 u_picaCoordSets;
uniform vec4 u_picaColorSources[6];
uniform vec4 u_picaAlphaSources[6];
uniform vec4 u_picaColorOperands[6];
uniform vec4 u_picaAlphaOperands[6];
uniform vec4 u_picaModes[6];
uniform vec4 u_picaFlags[6];
uniform vec4 u_picaConstants[6];
uniform vec4 u_picaBuffer;
uniform vec4 u_picaSpecial;

vec2 selectedUv(float code, vec2 uv0, vec2 uv1, vec2 uv2)
{
    if (code > 1.5) return uv2;
    if (code > 0.5) return uv1;
    return uv0;
}

vec4 tevSource(float code, vec4 primary, vec4 tex0, vec4 tex1, vec4 tex2, vec4 bufferValue, vec4 constantValue, vec4 previous)
{
    if (abs(code - 0.0) < 0.25 || abs(code - 1.0) < 0.25) return primary;
    if (abs(code - 3.0) < 0.25) return tex0;
    if (abs(code - 4.0) < 0.25) return tex1;
    if (abs(code - 5.0) < 0.25) return tex2;
    if (abs(code - 13.0) < 0.25) return bufferValue;
    if (abs(code - 14.0) < 0.25) return constantValue;
    if (abs(code - 15.0) < 0.25) return previous;
    return vec4(0.0);
}

vec4 colorOperand(vec4 value, float code)
{
    float baseCode = floor(code * 0.5) * 2.0;
    vec4 outValue = value;
    if (abs(baseCode - 2.0) < 0.25) outValue = value.aaaa;
    else if (abs(baseCode - 4.0) < 0.25) outValue = value.rrrr;
    else if (abs(baseCode - 8.0) < 0.25) outValue = value.gggg;
    else if (abs(baseCode - 12.0) < 0.25) outValue = value.bbbb;
    if (mod(floor(code + 0.5), 2.0) > 0.5) outValue = vec4(1.0) - outValue;
    return outValue;
}

float alphaOperand(vec4 value, float code)
{
    float baseCode = floor(code * 0.5) * 2.0;
    float outValue = value.a;
    if (abs(baseCode - 2.0) < 0.25) outValue = value.r;
    else if (abs(baseCode - 4.0) < 0.25) outValue = value.g;
    else if (abs(baseCode - 6.0) < 0.25) outValue = value.b;
    if (mod(floor(code + 0.5), 2.0) > 0.5) outValue = 1.0 - outValue;
    return outValue;
}

vec3 combineColor(float mode, vec4 aa, vec4 bb, vec4 cc)
{
    vec3 a = aa.rgb;
    vec3 b = bb.rgb;
    vec3 c = cc.rgb;
    if (mode < 0.5) return a;
    if (mode < 1.5) return a * b;
    if (mode < 2.5) return min(a + b, vec3(1.0));
    if (mode < 3.5) return clamp(a + b - vec3(0.5), vec3(0.0), vec3(1.0));
    if (mode < 4.5) return mix(b, a, c);
    if (mode < 5.5) return max(a - b, vec3(0.0));
    if (mode < 6.5) return vec3(min(dot(a, b), 1.0));
    if (mode < 7.5) return vec3(min(dot(aa, bb), 1.0));
    if (mode < 8.5) return min(a * b + c, vec3(1.0));
    if (mode < 9.5) return min(a + b, vec3(1.0)) * c;
    return a;
}

float combineAlpha(float mode, float a, float b, float c)
{
    if (mode < 0.5) return a;
    if (mode < 1.5) return a * b;
    if (mode < 2.5) return min(a + b, 1.0);
    if (mode < 3.5) return clamp(a + b - 0.5, 0.0, 1.0);
    if (mode < 4.5) return mix(b, a, c);
    if (mode < 5.5) return max(a - b, 0.0);
    if (mode < 6.5) return min(a * b * 3.0, 1.0);
    if (mode < 7.5) return min(a * b * 4.0, 1.0);
    if (mode < 8.5) return min(a * b + c, 1.0);
    if (mode < 9.5) return min(a + b, 1.0) * c;
    return a;
}

void main()
{
    vec4 tex0 = texture2D(s_picaTex0, selectedUv(u_picaCoordSets.x, v_texcoord0, v_texcoord1, v_texcoord2) + u_picaUvOffsets[0].xy);
    vec4 tex1 = texture2D(s_picaTex1, selectedUv(u_picaCoordSets.y, v_texcoord0, v_texcoord1, v_texcoord2) + u_picaUvOffsets[1].xy);
    vec4 tex2 = texture2D(s_picaTex2, selectedUv(u_picaCoordSets.z, v_texcoord0, v_texcoord1, v_texcoord2) + u_picaUvOffsets[2].xy);
    if (u_picaSpecial.x > 0.5)
    {
        vec4 outerColor = tex0 * vec4(u_tintCutoff.rgb, 1.0);
        outerColor.a = 1.0;
        gl_FragColor = outerColor;
        return;
    }
    vec4 primary = v_color0 * vec4(u_tintCutoff.rgb, 1.0);
    vec4 previous = vec4(0.0);
    vec4 bufferValue = u_picaBuffer;
    vec4 outputColor = previous;
    for (int stage = 0; stage < 6; ++stage)
    {
        vec4 ca = colorOperand(tevSource(u_picaColorSources[stage].x, primary, tex0, tex1, tex2, bufferValue, u_picaConstants[stage], previous), u_picaColorOperands[stage].x);
        vec4 cb = colorOperand(tevSource(u_picaColorSources[stage].y, primary, tex0, tex1, tex2, bufferValue, u_picaConstants[stage], previous), u_picaColorOperands[stage].y);
        vec4 cc = colorOperand(tevSource(u_picaColorSources[stage].z, primary, tex0, tex1, tex2, bufferValue, u_picaConstants[stage], previous), u_picaColorOperands[stage].z);
        float aa = alphaOperand(tevSource(u_picaAlphaSources[stage].x, primary, tex0, tex1, tex2, bufferValue, u_picaConstants[stage], previous), u_picaAlphaOperands[stage].x);
        float ab = alphaOperand(tevSource(u_picaAlphaSources[stage].y, primary, tex0, tex1, tex2, bufferValue, u_picaConstants[stage], previous), u_picaAlphaOperands[stage].y);
        float ac = alphaOperand(tevSource(u_picaAlphaSources[stage].z, primary, tex0, tex1, tex2, bufferValue, u_picaConstants[stage], previous), u_picaAlphaOperands[stage].z);
        outputColor.rgb = combineColor(u_picaModes[stage].x, ca, cb, cc);
        outputColor.a = combineAlpha(u_picaModes[stage].y, aa, ab, ac);
        outputColor.rgb = min(outputColor.rgb * u_picaModes[stage].z, vec3(1.0));
        outputColor.a = min(outputColor.a * u_picaModes[stage].w, 1.0);
        if (u_picaFlags[stage].x > 0.5) bufferValue.rgb = previous.rgb;
        if (u_picaFlags[stage].y > 0.5) bufferValue.a = previous.a;
        previous = outputColor;
    }
    if (u_picaSpecial.y > 0.5)
    {
        float keyAmount = smoothstep(0.004, 0.035, max(outputColor.r, max(outputColor.g, outputColor.b)));
        outputColor.a *= keyAmount;
        // The coastal outer archive stores its wet-sand input as neutral
        // gray. The game composites that layer with the warm sand input;
        // retain true black holes while restoring the visible sand hue.
        vec3 warmSand = tex1.rgb * vec3(0.78, 0.76, 0.72);
        outputColor.rgb = mix(outputColor.rgb, max(outputColor.rgb, warmSand), keyAmount);
    }
    if (u_picaSpecial.w > 1.5)
    {
        vec3 seaBuffer = clamp(
            tex0.rgb * vec3(0.45, 0.36, 0.59),
            vec3(0.0),
            vec3(1.0));
        outputColor.rgb = mix(
            seaBuffer,
            clamp(outputColor.rgb, vec3(0.0), vec3(1.0)),
            0.08);
    }
    else if (u_picaSpecial.w > 0.5)
    {
        outputColor.rgb = clamp(outputColor.rgb, vec3(0.0), vec3(1.0));
    }
    else
    {
        outputColor.rgb = pow(clamp(outputColor.rgb, vec3(0.0), vec3(1.0)), vec3(2.2));
    }
    outputColor.rgb *= u_picaSpecial.z;
    if (outputColor.a < u_tintCutoff.a) discard;
    gl_FragColor = outputColor;
}
