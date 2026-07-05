// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rain-on-glass pack, main pass: the Gaussian-blurred backdrop (buffer 1)
// as fog, with procedural rain droplets running down the pane. Two moving
// droplet layers at different scales plus one static bead layer; each
// droplet contributes a local offset vector (pointing at its centre) that
// refracts the fogged scene, and a small top-light highlight sells the
// lens. Falling drops wiggle side to side and leave a fading trail of
// beads above them, the classic filmed-window look. All motion is
// hash-derived from iTime, so there is no per-frame state. Same slab
// composite as the blur family.
//
// handlesOpacity: the window sample is dimmed by uSurfaceOpacity here, so
// the pane stays solid and translucency reveals the rain behind.
// DAEMON FALLBACK: no scene behind daemon surfaces (uHasBackdrop = 0), so
// the droplets light a dark glass slab instead of refracting a capture.
//
// ANIMATED (references iTime): metadata declares "animated": true so the
// daemon host ticks the item; the compositor detects the linked iTime
// uniform itself and repaints the window continuously while decorated.

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

const float kTau = 6.28318530718;

float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// px-space (top-down) vector -> canvas UV offset (vTexCoord is Y-up).
vec2 pxToUv(vec2 v) {
    return vec2(v.x, -v.y) / max(uSurfaceSize, vec2(1.0));
}

// High-quality hashes (same base as frosted-glass).
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 hash32(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xxy + p3.yzz) * p3.zyx);
}

// One layer of falling droplets on a tall cell grid. `st` is glass-space
// (device px / cell size). Returns (offset.xy, wetness): offset points from
// the fragment toward the droplet centre (in cell units), wetness masks
// where glass is wet (droplet body or trail beads).
vec3 dropLayer(vec2 st, float t) {
    // Tall cells: droplets own a column taller than wide, so trails read
    // vertically. Per-column phase decorrelates neighbouring columns.
    vec2 cellDim = vec2(1.0, 0.25); // cell = 1 wide x 4 tall in st units
    vec2 grid = st / cellDim;
    grid.y += t; // the whole layer's droplets fall together; phase varies below
    vec2 id = floor(grid);
    float colPhase = hash12(vec2(id.x, 17.0));
    grid.y += colPhase * 9.0;
    id = floor(grid);
    vec3 n = hash32(id);
    vec2 f = fract(grid) - vec2(0.5, 0.0);

    // Droplet x: parked off-centre per cell, wiggling as it falls; the
    // wiggle amplitude shrinks near the cell walls so drops stay inside.
    float y01 = f.y; // 0 bottom .. 1 top of the cell
    float wiggle = sin(y01 * kTau * 2.0 + n.z * kTau);
    float x = (n.x - 0.5) * 0.6 + wiggle * 0.12 * (0.5 - abs(n.x - 0.5));

    // Droplet y: eases down the cell over its cycle (fract of the layer
    // scroll), pausing near the top like surface tension letting go.
    float dy = 1.0 - pow(1.0 - y01, 2.0);
    vec2 dropPos = vec2(x, dy * 0.8 + 0.05);
    vec2 toDrop = (f - dropPos) * vec2(1.0, cellDim.y / cellDim.x);
    float dist = length(toDrop);
    float dropR = 0.055 * (0.7 + 0.6 * n.y);
    float drop = smoothstep(dropR, dropR * 0.6, dist);

    // Trail: a fading chain of beads above the droplet along its column.
    vec2 trailF = vec2(f.x - x, fract(f.y * 8.0) - 0.5);
    float trailDist = length(trailF * vec2(1.0, 0.35));
    float beads = smoothstep(0.035, 0.02, trailDist);
    float aboveDrop = smoothstep(dropPos.y, dropPos.y + 0.35, f.y);
    float trail = beads * aboveDrop * smoothstep(1.0, dropPos.y, f.y) * 0.5;

    vec2 offset = -toDrop * drop; // refract toward the droplet centre
    return vec3(offset, clamp(drop + trail, 0.0, 1.0));
}

void main() {
    vec4 window = surfaceTexel(vTexCoord) * uSurfaceOpacity;

    vec2 px = surfacePixel(vTexCoord);
    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 center = uSurfaceFrameTopLeft + halfSz;
    float radius = clamp(p_cornerRadius * uSurfaceScale, 0.0, min(halfSz.x, halfSz.y));
    float d = sdRoundedBox(px - center, halfSz, radius);
    float mask = 1.0 - smoothstep(-1.0, 1.0, d);

    // Glass space: device px scaled so dropletScale 1.0 gives ~90 px cells.
    float cellPx = 90.0 * clamp(p_dropletScale, 0.25, 4.0) * uSurfaceScale;
    vec2 st = (px - uSurfaceFrameTopLeft) / cellPx;
    float t = iTime * max(p_rainSpeed, 0.0);

    // Two falling layers at offset scales/speeds plus one static bead layer;
    // rainAmount thins the field by culling cells on a hash.
    vec3 layer1 = dropLayer(st, t);
    vec3 layer2 = dropLayer(st * 1.7 + 4.3, t * 1.3);
    float cull1 = step(1.0 - clamp(p_rainAmount, 0.0, 1.0), hash12(floor(st * vec2(1.0, 4.0)) + 2.7));
    float cull2 = step(1.0 - clamp(p_rainAmount, 0.0, 1.0), hash12(floor(st * 1.7 * vec2(1.0, 4.0)) + 8.1));
    layer1 *= cull1;
    layer2 *= cull2;

    // Static micro-beads: tiny fixed droplets that never move, filling the
    // pane so dry stretches still read as wet glass.
    vec2 microId = floor(st * 6.0);
    vec3 mn = hash32(microId);
    vec2 microF = fract(st * 6.0) - 0.5;
    vec2 toMicro = microF - (mn.xy - 0.5) * 0.7;
    float micro = smoothstep(0.06, 0.035, length(toMicro)) * step(mn.z, 0.35 * clamp(p_rainAmount, 0.0, 1.0));

    vec2 offset = (layer1.xy + layer2.xy - toMicro * micro) * cellPx;
    float wet = clamp(layer1.z + layer2.z + micro, 0.0, 1.0);

    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Fog everywhere; droplets refract the fogged scene toward their
        // centres (the blurred buffer keeps the lensed image soft and cheap).
        vec2 uv = clamp(vTexCoord + pxToUv(offset * p_refraction / max(cellPx, 1.0)), 0.0, 1.0);
        vec4 fog = texture(iChannel1, uv);
        // Top-light: a small highlight on each droplet's upper edge.
        float hi = wet * clamp(-offset.y / max(cellPx, 1.0) * 6.0, 0.0, 1.0) * 0.25;
        pane = vec4(fog.rgb + hi * fog.a, fog.a) * mask;
    } else {
        // Original pseudo look for daemon surfaces: droplets glint over a
        // dark glass slab.
        vec3 slab = vec3(0.10, 0.11, 0.14) + wet * 0.12;
        pane = vec4(slab, 1.0) * 0.5 * mask;
    }

    fragColor = window + pane * (1.0 - window.a);
}
