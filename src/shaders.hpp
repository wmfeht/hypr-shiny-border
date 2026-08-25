#pragma once

#include <string>

// Hyprland tex300.vert — unit quad, proj maps it onto the decoration box.
inline const std::string SHINY_VERT = R"#(#version 300 es

uniform mat3 proj;
uniform vec4 color;

in vec2 pos;
in vec2 texcoord;
in vec2 texcoordMatte;

out vec4 v_color;
out vec2 v_texcoord;
out vec2 v_texcoordMatte;

void main() {
    gl_Position = vec4(proj * vec3(pos, 1.0), 1.0);
    v_color = color;
    v_texcoord = texcoord;
    v_texcoordMatte = texcoordMatte;
}
)#";

// Conic comet on a rounded-rect SDF ring. Uniform names match CShader's lookup table.
inline const std::string SHINY_FRAG = R"#(#version 300 es
precision highp float;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

uniform vec4  color;             // col.a, highlight head (straight alpha)
uniform vec4  colorSRGB;         // col.b, shoulder
uniform vec2  topLeft;
uniform vec2  fullSize;
uniform float radius;
uniform float radiusOuter;
uniform float roundingPower;
uniform float thick;
uniform float time;
uniform float alpha;
uniform float range;             // angular half-width as fraction of the circle (0.08–0.45)
uniform float brightness;        // pulse Hz; <= 0 is the nominal ring
uniform float angle;             // latched heading, radians, already quantized + offset

const float TAU = 6.28318530718;
const float AA  = 1.25;

float sdRoundBox(vec2 p, vec2 b, float r, float power) {
    vec2 q = abs(p) - b + vec2(r);
    vec2 qp = max(q, 0.0);
    float outside;
    if (power < 2.01)
        outside = length(qp);
    else
        outside = pow(pow(max(qp.x, 0.0), power) + pow(max(qp.y, 0.0), power), 1.0 / power);
    return outside + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec2 center = topLeft + fullSize * 0.5;
    vec2 p      = gl_FragCoord.xy - center;

    float heading = angle;

    float ang = atan(-p.y, p.x);
    float t   = fract((ang - heading) / TAU); // 0 at the mouse-facing head
    float d0  = min(t, 1.0 - t);              // 0..0.5 around the circle

    float pulse, spread, thickNow;
    if (brightness <= 0.0) {
        spread   = max(range, 0.04);
        thickNow = thick;
    } else {
        pulse    = 0.5 + 0.5 * sin(time * brightness * TAU);
        spread   = max(mix(range * 0.45, range * 1.35, pulse), 0.04);
        thickNow = thick * mix(0.78, 1.18, pulse);
    }

    float cone = 1.0 - smoothstep(0.0, spread, d0);
    cone       = pow(max(cone, 0.0), 1.65);

    // Thickness breathes, and the comet is locally thicker than the rest of the ring.
    float localT   = mix(thickNow * 0.38, thickNow, mix(0.15, 1.0, cone));
    localT         = max(localT, 1.0);

    float rOut = max(radiusOuter, 0.0);
    float rIn  = max(rOut - localT, 0.0);
    vec2  bOut = fullSize * 0.5;
    vec2  bIn  = max(bOut - vec2(localT), vec2(0.5));

    float dOut  = sdRoundBox(p, bOut, rOut, roundingPower);
    float dIn   = sdRoundBox(p, bIn, rIn, roundingPower);

    // Client area: inside the inner contour. Never paint window contents.
    if (dIn < -AA)
        discard;

    // Ring: inside the outer contour, outside the inner contour.
    float ring = smoothstep(AA, -AA, dOut) * smoothstep(-AA, AA, dIn);
    // Halo strictly outside the rounded rect (AABB corners). dOut > 0 is outside.
    float glow = (1.0 - smoothstep(0.0, localT * 1.35, dOut)) * smoothstep(0.0, AA, dOut) * cone;

    float cov = max(ring, glow * 0.65);
    if (cov < 0.002)
        discard;

    // Dim tail is a dark edge; the comet core blows toward white.
    float hot = pow(cone, 2.6);
    vec3  dim = colorSRGB.rgb * 0.22;
    vec3  rgb = mix(mix(dim, color.rgb, pow(cone, 0.9)), vec3(1.0), hot * 0.95);
    float a   = cov * mix(0.055, 1.0, mix(pow(cone, 1.15), hot, 0.45)) * alpha;
    fragColor = vec4(rgb * a, a);
}
)#";
