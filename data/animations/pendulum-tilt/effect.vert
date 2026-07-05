// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pendulum-move vertex shader — rigid rotation against the direction of
// travel, like a card carried by its top edge. Driven by the HELD
// transition motion contract: the swing angle follows the spring-smoothed
// horizontal velocity (iMoveVelocity.x), and because that velocity is
// deliberately underdamped C++-side it rings through zero after release,
// so the window swings back past upright once and settles — a pendulum,
// with no per-frame state in the shader. The pivot sits on the live frame
// rect (iToRect, re-stamped every held frame by the paint pipeline).

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
uniform vec4 iToRect;
#endif

const float kDegToRad = 0.01745329252;

void main() {
#ifdef PLASMAZONES_KWIN
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);

    // Swing against horizontal travel: moving right swings the body left
    // of the pivot (negative rotation in y-down screen space), so the
    // window trails like a hanging card. ~800 px/s of drag at default
    // sensitivity reaches the default 6-degree cap.
    float maxRad = clamp(p_maxAngle, 0.5, 20.0) * kDegToRad;
    float ang = clamp(-iMoveVelocity.x * p_sensitivity * 1.3e-4, -maxRad, maxRad);

    // Pivot on the live frame rect: horizontally centred, vertically at
    // the configured hinge height (top edge by default — the title bar).
    vec2 pivot = vec2(iToRect.x + 0.5 * iToRect.z, iToRect.y + clamp(p_pivotHeight, 0.0, 1.0) * iToRect.w);
    vec2 rel = position - pivot;
    float c = cos(ang);
    float s = sin(ang);
    vec2 displaced = pivot + vec2(rel.x * c - rel.y * s, rel.x * s + rel.y * c);

    vTexCoord = cuv;
    gl_Position = modelViewProjectionMatrix * vec4(displaced, 0.0, 1.0);
#else
    // Daemon RHI bake target: compositor-only motion — pass through.
    vTexCoord = texCoord;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
#endif
}
