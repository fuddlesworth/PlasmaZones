// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared easing curves for animation shaders, hosted here so the per-shader
// copies (bmw_compat.glsl, tv, pixel-wipe, aura-glow) collapse to one
// definition. These four curves are the canonical Robert Penner polynomial
// maps (the same forms Burn-My-Windows uses) and carry no BMW-specific
// derivation — the same rationale as noise.glsl's hash12 — so this file is
// LGPL-2.1-or-later and may be included from LGPL shaders; bmw_compat.glsl
// (GPL) also includes it, which is fine — a GPL work may include an LGPL
// header.
//
// Include AFTER the animation uniform block (the harness prepends
// <animation_uniforms.glsl>); these functions reference no uniforms, so the
// order only matters for consistency.
#ifndef PHOSPHOR_EASING_GLSL
#define PHOSPHOR_EASING_GLSL

float easeOutQuad(float x) { return -1.0 * x * (x - 2.0); }
float easeInQuad(float x)  { return x * x; }
float easeOutCubic(float t) {
    float f = t - 1.0;
    return f * f * f + 1.0;
}
float easeInOutCubic(float t) {
    return t < 0.5 ? 4.0 * t * t * t : (t - 1.0) * (2.0 * t - 2.0) * (2.0 * t - 2.0) + 1.0;
}

#endif // PHOSPHOR_EASING_GLSL
