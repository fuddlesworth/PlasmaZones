// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reusable rounded-corner / concave-carve helpers for PhosphorShell
// panel and popup shaders. `#include "corners.glsl"` to get rounded-box
// surface masking and the carved panel-outline distance field without
// copying the SDF math into every shader.
//
// No `#version` / no UBO references — include this AFTER the `#version`
// line and the `layout` blocks of the consuming shader.

#ifndef PHOSPHOR_CORNERS_GLSL
#define PHOSPHOR_CORNERS_GLSL

// Signed distance from `p` (relative to the box centre) to a rounded
// box of half-extent `halfSize` and corner `radius`. Negative inside,
// positive outside.
float roundedBoxSDF(vec2 p, vec2 halfSize, float radius) {
    vec2 d = abs(p) - halfSize + radius;
    return length(max(d, 0.0)) - radius;
}

// Antialiased 0..1 coverage for a rounded-box surface of size
// `resolution`, evaluated at framebuffer coord `fragCoord`. `radius`
// must be >= 1.0 — the rounded-box SDF degenerates at r == 0 (interior
// fragments return 0 rather than a negative distance, which would zero
// the whole surface out).
float roundedSurfaceMask(vec2 fragCoord, vec2 resolution, float radius) {
    vec2 halfSize = resolution * 0.5;
    float dist = roundedBoxSDF(fragCoord - halfSize, halfSize, radius);
    return 1.0 - smoothstep(-1.0, 0.0, dist);
}

// Signed distance (pixels) from the panel's bottom outline at the
// top-down surface coord `vPx`, for a panel whose flat bottom edge sits
// at `panelBottomYPx`. When `carveRpx > 0.5` each bottom corner is
// replaced by a concave quarter-arc of that radius; the outline stays a
// single continuous field, so a drop-shadow measured from it follows
// the carve instead of leaving a rectangular strip behind a curved
// edge. Negative = inside the panel, positive = below the outline.
float carvedOutlineDistance(vec2 vPx, vec2 resolution, float panelBottomYPx, float carveRpx) {
    if (carveRpx > 0.5 && vPx.y > panelBottomYPx - carveRpx) {
        // Bottom-left arc
        if (vPx.x < carveRpx) {
            vec2 arcCenter = vec2(carveRpx, panelBottomYPx - carveRpx);
            return length(vPx - arcCenter) - carveRpx;
        }
        // Bottom-right arc
        if (vPx.x > resolution.x - carveRpx) {
            vec2 arcCenter = vec2(resolution.x - carveRpx, panelBottomYPx - carveRpx);
            return length(vPx - arcCenter) - carveRpx;
        }
    }
    // Middle column, above the carve band, or carve disabled — the
    // outline is the flat horizontal bottom edge.
    return vPx.y - panelBottomYPx;
}

#endif // PHOSPHOR_CORNERS_GLSL
