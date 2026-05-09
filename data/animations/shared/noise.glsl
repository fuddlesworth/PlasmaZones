// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared noise primitives for animation shaders.
//
// Hosted here so the per-shader copies (previously duplicated verbatim
// across apparition, aura-glow, doom, energize-a, energize-b, hexagon,
// matrix, pixel-wipe) collapse to one definition. Including this from a
// shader brings hash22 + simplex2D + simplex2DFractal into scope.
//
// `simplex2D` is the standard 2D simplex-noise variant used across the
// suite. It returns a value in roughly [0, 1] (the 0.5 + 0.5 * ... shift
// at the end normalises the [-1, 1] simplex output). `simplex2DFractal`
// is a 4-octave fBm wrapper using the canonical rotation matrix
// mat2(1.6, 1.2, -1.2, 1.6) per octave.
//
// `hash22` is a 2-component hash using the Inigo Quilez "fract(sin(...))"
// pattern; deterministic per input vec2, output in [0, 1).

#ifndef PHOSPHOR_NOISE_GLSL
#define PHOSPHOR_NOISE_GLSL

// `hash22`'s `fract(p3 * 0.1031)` chain operates on
// per-fragment-pixel inputs at large magnitudes; on drivers that
// default fragment precision to `mediump` (some classic-GL stacks
// the kwin-effect path runs on), the multiplication aliases past the
// 24-bit mantissa and the noise output becomes blocky/banded. The
// daemon RHI path defaults to highp and is unaffected. Pin the
// precision here so callers across both runtimes get the same
// quality. The `#version 450` daemon path treats `precision` as a
// no-op; the `#version 100`/`100 es` kwin path enforces it.
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#endif

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float simplex2D(vec2 p) {
    const float K1 = 0.366025404;
    const float K2 = 0.211324865;
    vec2 i  = floor(p + (p.x + p.y) * K1);
    vec2 a  = p - i + (i.x + i.y) * K2;
    float m = step(a.y, a.x);
    vec2 o  = vec2(m, 1.0 - m);
    vec2 b  = a - o + K2;
    vec2 c  = a - 1.0 + 2.0 * K2;
    vec3 h  = max(0.5 - vec3(dot(a, a), dot(b, b), dot(c, c)), 0.0);
    vec3 n  = h * h * h * h *
            vec3(dot(a, -1.0 + 2.0 * hash22(i + 0.0)),
                 dot(b, -1.0 + 2.0 * hash22(i + o)),
                 dot(c, -1.0 + 2.0 * hash22(i + 1.0)));
    return 0.5 + 0.5 * dot(n, vec3(70.0));
}

float simplex2DFractal(vec2 p) {
    mat2 m  = mat2(1.6, 1.2, -1.2, 1.6);
    float f = 0.5000 * simplex2D(p);  p = m * p;
    f      += 0.2500 * simplex2D(p);  p = m * p;
    f      += 0.1250 * simplex2D(p);  p = m * p;
    f      += 0.0625 * simplex2D(p);
    return f;
}

#endif
