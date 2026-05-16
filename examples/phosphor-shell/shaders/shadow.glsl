// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reusable drop-shadow helpers for PhosphorShell panel/popup shaders.
// `#include "shadow.glsl"` to render a shadow without copying the
// falloff math:
//   - shadowStripAlpha() — a one-edge strip shadow below a panel that
//     is flush with a screen edge. Pair with corners.glsl's
//     carvedOutlineDistance() so the strip follows a carved corner.
//   - dropShadowAlpha() — an all-around soft shadow ringing a floating
//     surface (popup). Pair with corners.glsl's roundedBoxSDF().
//
// No `#version` / no UBO references — include this AFTER the `#version`
// line and the `layout` blocks of the consuming shader.

#ifndef PHOSPHOR_SHADOW_GLSL
#define PHOSPHOR_SHADOW_GLSL

// Drop-shadow alpha for a fragment `distFromOutline` pixels below the
// panel outline. The shadow strip is `shadowSizePx` tall; alpha is
// `shadowOpacity` right at the panel edge and falls off quadratically
// to 0 at the far end of the strip. Returns 0 for fragments inside the
// panel, beyond the strip, or when there is no strip (shadowSizePx 0).
float shadowStripAlpha(float distFromOutline, float shadowSizePx, float shadowOpacity) {
    // 0.5-px slack: the panel branch of the consumer handles outline AA
    // on the inside, so the shadow only owns fragments clearly outside.
    if (distFromOutline <= 0.5 || shadowSizePx <= 0.0 || distFromOutline >= shadowSizePx) {
        return 0.0;
    }
    float t = distFromOutline / shadowSizePx;
    float falloff = 1.0 - t;
    return shadowOpacity * falloff * falloff;
}

// Soft drop-shadow alpha for a fragment `sdfDist` pixels OUTSIDE a
// rounded box (positive = outside; pass roundedBoxSDF() from
// corners.glsl). Alpha is `shadowOpacity` right at the box edge and
// falls off quadratically to 0 over `margin` pixels. Returns 0 inside
// the box. For a floating surface (popup), oversize the surface by
// `margin` on every side, make the visible element the inset rounded
// box, and ring it with this.
float dropShadowAlpha(float sdfDist, float margin, float shadowOpacity) {
    if (sdfDist <= 0.0 || margin <= 0.0) {
        return 0.0;
    }
    float t = clamp(sdfDist / margin, 0.0, 1.0);
    float falloff = 1.0 - t;
    return shadowOpacity * falloff * falloff;
}

#endif // PHOSPHOR_SHADOW_GLSL
