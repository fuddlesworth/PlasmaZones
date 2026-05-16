// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reusable drop-shadow helper for PhosphorShell panel shaders.
// `#include "shadow.glsl"` to render the panel's drop-shadow strip
// without copying the falloff math. Pair with corners.glsl: feed the
// `carvedOutlineDistance()` result in so the shadow follows a carved
// corner.
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

#endif // PHOSPHOR_SHADOW_GLSL
