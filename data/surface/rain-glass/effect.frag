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

#include <surface_multipass.glsl>
#include <surface_noise.glsl>

// Droplet cell shape: cells ~2.5x taller than wide so each drop has a real
// run and trails read vertically. Shared by dropLayer and the per-cell
// culling in pSurface.
const vec2 kRainCellDim = vec2(1.0, 2.5);

// One layer of falling droplets on a tall cell grid. `st` is glass-space
// (device px / cell size, y down). Returns (offset.xy, wetness): offset
// points from the fragment toward the droplet centre (in st units),
// wetness masks where glass is wet (droplet body or trail beads).
//
// The drop's position is a function of TIME, not of the cell coordinate:
// each cell runs its own phase-shifted cycle (ti) during which its drop
// travels the full cell height with judder and side-to-side wiggle, so
// the motion is unmistakable at any speed — the previous revision derived
// the position from the cell coordinate alone, which only scrolled the
// whole field a few px/s and read as a still image.
vec3 dropLayer(vec2 st, float t) {
    vec2 cellDim = kRainCellDim;
    vec2 grid = st / cellDim;
    vec2 id = floor(grid);
    vec2 f = fract(grid); // 0..1 in-cell, y = 0 at the top
    vec3 n = hash23(id);

    // Per-cell fall cycle: phase-shifted so neighbours never sync. Judder
    // (small stick-slip along the run) sells surface tension.
    float ti = fract(t + n.z);
    float fall = ti + 0.06 * sin(ti * 34.0 + n.y * TAU) * ti * (1.0 - ti);
    float dropY = mix(0.06, 0.94, clamp(fall, 0.0, 1.0));

    // X: parked off-centre per cell, wiggling as the drop falls.
    float x = 0.5 + (n.x - 0.5) * 0.5 + sin(fall * TAU * 2.0 + n.y * TAU) * 0.08;

    // Droplet body — distances back in st units so drops stay round.
    vec2 toDrop = (f - vec2(x, dropY)) * cellDim;
    float dropR = 0.10 * (0.75 + 0.5 * n.y);
    float drop = smoothstep(dropR, dropR * 0.5, length(toDrop));

    // Trail: a chain of beads strictly ABOVE the drop (smaller y), fading
    // with distance above it and drying out late in the fall.
    vec2 toBead = vec2(f.x - x, (fract(f.y * 12.0) - 0.5) / 12.0) * cellDim;
    float beads = smoothstep(0.055, 0.03, length(toBead));
    float above = step(f.y, dropY) * smoothstep(dropY - 0.55, dropY, f.y);
    float trail = beads * above * (1.0 - 0.5 * fall);

    vec2 offset = -toDrop * drop; // refract toward the droplet centre
    return vec3(offset, clamp(drop + trail * 0.6, 0.0, 1.0));
}

vec4 pSurface(vec2 uv) {
    SurfaceSlab slab = surfaceSlabOpen(uv, p_cornerRadius * uSurfaceScale);
    vec2 px = slab.px;
    float mask = slab.mask;

    // Glass space: device px scaled so dropletScale 1.0 gives ~90 px cells.
    float cellPx = 90.0 * clamp(p_dropletScale, 0.25, 4.0) * max(uSurfaceScale, 0.001);
    vec2 st = (px - uSurfaceFrameTopLeft) / cellPx;
    float t = iTime * max(p_rainSpeed, 0.0);

    // Two falling layers at offset scales/speeds plus one static bead layer;
    // rainAmount thins the field by culling whole cells on a hash.
    float amount = clamp(p_rainAmount, 0.0, 1.0);
    vec3 layer1 = dropLayer(st, t);
    vec3 layer2 = dropLayer(st * 1.6 + 4.3, t * 1.35);
    layer1 *= step(1.0 - amount, hash13(floor(st / kRainCellDim) + 2.7));
    layer2 *= step(1.0 - amount, hash13(floor((st * 1.6 + 4.3) / kRainCellDim) + 8.1));

    // Static micro-beads: tiny fixed droplets that never move, filling the
    // pane so dry stretches still read as wet glass.
    vec2 microId = floor(st * 6.0);
    vec3 mn = hash23(microId);
    vec2 microF = fract(st * 6.0) - 0.5;
    vec2 toMicro = microF - (mn.xy - 0.5) * 0.7;
    float micro = smoothstep(0.06, 0.035, length(toMicro)) * step(mn.z, 0.35 * amount);

    // Combined refraction offset in device px. layer1/layer2 offsets are in st
    // units, so ×cellPx maps them to px. toMicro lives in the 6×-denser micro-bead
    // grid, so the same ×cellPx intentionally over-scales the micro term by ~6×;
    // that exaggerated push, gated to a subtle secondary lens by the *0.3, is what
    // makes the tiny fixed beads catch the light. Do not "correct" the scale
    // without checking the look.
    vec2 offsetPx = (layer1.xy + layer2.xy - toMicro * micro * 0.3) * cellPx;
    float wet = clamp(layer1.z + layer2.z + micro, 0.0, 1.0);

    vec4 pane;
    if (uHasBackdrop >= 0.5) {
        // Fog everywhere; droplets refract the fogged scene toward their
        // centres (the blurred buffer keeps the lensed image soft and cheap).
        // refraction 40 = the geometric offset as-is; other values scale it.
        vec2 sampleUv = clamp(uv + pxToUv(offsetPx * (p_refraction / 40.0)), 0.0, 1.0);
        vec4 fog = texture(iChannel1, sampleUv);
        // Top-light: a small highlight on each droplet's upper edge (the
        // offset points down toward the centre there, so +y in px space).
        float hi = wet * clamp(offsetPx.y / max(cellPx, 1.0) * 8.0, 0.0, 1.0) * 0.3;
        pane = vec4(fog.rgb + hi * fog.a, fog.a) * mask;
    } else {
        // Original pseudo look for daemon surfaces: droplets glint over a
        // dark glass slab.
        vec3 glassSlab = vec3(0.10, 0.11, 0.14) + wet * 0.12;
        pane = vec4(glassSlab, 1.0) * 0.5 * mask;
    }

    return slabComposite(slab.window, pane);
}
