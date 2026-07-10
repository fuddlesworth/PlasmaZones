// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared noise primitives for animation shaders.
//
// Hosted here so the per-shader copies (previously duplicated verbatim
// across apparition, aura-glow, doom, energize-a, energize-b, hexagon,
// matrix, pixel-wipe) collapse to one definition. Including this from a
// shader brings hash22 + simplex2D + simplex2DFractal + surfaceSeed
// into scope.
//
// `simplex2D` is the standard 2D simplex-noise variant used across the
// suite. It returns a value in roughly [0, 1] (the 0.5 + 0.5 * ... shift
// at the end normalises the [-1, 1] simplex output). `simplex2DFractal`
// is a 4-octave fBm wrapper using the canonical rotation matrix
// mat2(1.6, 1.2, -1.2, 1.6) per octave.
//
// `hash22` is a 2-component hash using the Inigo Quilez "fract(sin(...))"
// pattern; deterministic per input vec2, output in [0, 1).
//
// `surfaceSeed()` returns a pseudo-random scalar in [0, 1) derived from
// `iSurfaceScreenPos.xy`. It substitutes for niri's `niri_random_seed`
// uniform in ported transitions, with one important behavioural
// difference: niri's seed is fresh per animation leg (each open or
// close gets a new random), while ours is keyed only on the surface's
// screen origin. So a window animated repeatedly at the same screen
// position will replay the SAME seed every leg — repeated open/close
// at one location is visually deterministic. Different surfaces (or
// the same surface moved to a new position) get different seeds. This
// trade-off keeps the function pure-uniform (no leg-attach plumbing
// needed) at the cost of cross-leg variability; ports that need true
// per-leg randomness should mix in a leg-unique input themselves.
//
// Requires `iSurfaceScreenPos` to be in scope, which means the caller
// must include `<animation_uniforms.glsl>` BEFORE `<noise.glsl>`.
// Both runtimes (daemon RHI and kwin-effect via KWin::GLShader) compile
// shader sources at `#version 450 core`, where all floats are 32-bit
// IEEE-754 by default — no precision-coupling concerns to track.

#ifndef PHOSPHOR_NOISE_GLSL
#define PHOSPHOR_NOISE_GLSL

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

// 1D hash from a vec2 using the Inigo Quilez `fract(p3 * 0.1031)` chain
// (the scalar-output sibling of hash22 above; distinct from niriHash's
// `sin(dot(...))` pattern below). Byte-equivalent to Burn-My-Windows'
// common.glsl hash12, so bmw_compat.glsl pulls this in rather than
// carrying its own copy — the single LGPL definition every consumer
// shares (aura-glow directly, tv-glitch / wisps via bmw_compat).
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
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

float surfaceSeed() {
    return fract(sin(dot(iSurfaceScreenPos.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

// niri-style 1D hash from vec2: the classic `fract(sin(dot(p,
// (127.1, 311.7))) * 43758.5453)` pattern. Used by the niri-derived
// ports for per-cell / per-instance pseudo-random in [0, 1). Lifted
// from the file-scope copies that previously lived in dissolve,
// glitch, ink-splash, plasma-flow, smoke, snap, and soft-warp-fade
// — all bit-equivalent except for sub-ULP variance in the
// constant's last decimals (43758.5453 vs 43758.5453123, identical
// in float32). Other niri ports keep their own hashes when the
// constants are deliberately different: static-fade uses unique magic
// constants; perlin pre-mods the dot product. The (12.9898, 78.233)
// sin-dot variant is shared just below as `classicHash`. Voronoi-shatter's
// vs_hash2 returns vec2 and stays local.
float niriHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Classic Inigo Quilez `fract(sin(dot(p, (12.9898, 78.233))) * 43758.5453)`
// value hash — the OTHER canonical sin-dot magic-constant pair (niriHash
// above uses (127.1, 311.7)). Lifted from the byte-identical per-shader
// copies that lived in crosshatch (crosshatch_rand), randomsquares
// (rs_rand), heat-melt (hm_rand), and the desktop packs dissolve /
// crosszoom / aretha (pz_hash, pz_rand). Deliberate variants that must
// NOT use this stay local: perlin
// pre-mods the dot product, static-fade uses unique constants,
// voronoi-shatter returns a vec2.
float classicHash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// niri-style bilinear value noise on niriHash (smooth-step interp
// at integer lattice corners). Used by the 4 niri ports that need
// procedural noise — plasma-flow, soft-warp-fade, ink-splash,
// smoke. Identical body across all four; lifting deduplicates ~10
// lines per shader. Perlin's perlin_noise stays local because it
// uses an alternative bilinear formulation tied to perlin_random's
// non-shareable hash.
float niriNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(niriHash(i),                    niriHash(i + vec2(1.0, 0.0)), f.x),
               mix(niriHash(i + vec2(0.0, 1.0)),   niriHash(i + vec2(1.0, 1.0)), f.x), f.y);
}

// Parameterised fBm over niriNoise: `octaves` layers with amplitude
// halving (gain 0.5) and frequency scaled by `lacunarity` per octave.
// Reproduces the two niri ports' hand-rolled loops exactly — ink-splash
// uses fbm(p, 5, 2.1) (its is_fbm) and smoke uses fbm(p, 6, 2.0) (its
// sm_fbm), which share this skeleton and differ only in octave count and
// lacunarity. Starting amplitude 0.5 and gain 0.5 are the shared constants.
float fbm(vec2 p, int octaves, float lacunarity) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < octaves; i++) {
        v += amp * niriNoise(p);
        p *= lacunarity;
        amp *= 0.5;
    }
    return v;
}

// Boundary mask for shaders that displace sample UVs. Returns 1.0
// when uv is fully within [0, 1] and fades to 0 over a 0.005-wide
// band placed JUST OUTSIDE the [0, 1] boundary. uTexture0's
// clamp-to-edge sampler would otherwise smear transparent
// window-edge pixels (alpha = 0 from rounded corners / drop
// shadows) into the rendered output, producing a grey-transparent
// border around the warped silhouette.
//
// Bands sit OUTSIDE [0, 1] so identity sampling (sample_uv ∈
// [0, 1]) gets mask = 1 everywhere — no inner-edge alpha clipping.
// Used by morph, popin, fade, inkwell-drop, plasma-flow, ripple,
// smoke, snap, soft-warp-fade, and glide. See PR #425 for the
// inside-vs-outside-band fix history.
float boundaryMask(vec2 uv) {
    vec2 lo = smoothstep(vec2(-0.005), vec2(0.0),   uv);
    vec2 hi = vec2(1.0) - smoothstep(vec2(1.0),     vec2(1.005), uv);
    return lo.x * lo.y * hi.x * hi.y;
}

// Pixel-accurate variant of boundaryMask for rigid-translation shaders
// (bounce, fly-in). Returns 1.0 inside the [0, 1] anchor and antialiases
// each edge across a band ONE device pixel wide, centred on the edge.
//
// boundaryMask places its fixed 0.005-of-UV feather band ENTIRELY
// outside [0, 1]. When the sampled texture has real content past its
// border that is fine — the kwin path's redirected FBO carries the
// window's shadow there, so the band fades into the shadow. But the
// daemon captures the anchor into an exactly anchor-sized,
// clamp-to-edge texture: there is nothing past the border, so the band
// multiplies a fading alpha onto the clamped OPAQUE edge texel and
// paints a visible halo of edge colour — the "grey border" around a
// translating OSD. Worse, the band is 0.005 of the anchor, so it widens
// with the surface (5 px on a 1000 px card).
//
// fwidth() makes the band exactly one pixel wide regardless of anchor
// size, and centring it on the geometric edge makes this a true
// antialiased crop: fully-covered interior pixels stay at 1.0 (no
// inner-edge fade), the edge pixel gets its real fractional coverage,
// and the sub-pixel outside half is ordinary edge AA, not a halo.
float boundaryMaskAA(vec2 uv) {
    vec2 fw = max(fwidth(uv), vec2(1.0e-5));
    vec2 lo = smoothstep(-0.5 * fw, 0.5 * fw, uv);
    vec2 hi = smoothstep(-0.5 * fw, 0.5 * fw, vec2(1.0) - uv);
    return lo.x * lo.y * hi.x * hi.y;
}

// Pointy-top hex-grid helpers shared by the Aretha packs (aretha-materialize
// and desktop-aretha), ported from data/shaders/aretha-shell. `hexDist`
// returns 0 at a cell centre rising to ~0.5 at the edge; `hexLocal` returns
// the offset from the nearest hex-cell centre for a point in hex-grid space.
// Distinct from the hexagon / honeycomb packs, which use different hex
// formulations and keep their own math.
float hexDist(vec2 p) {
    p = abs(p);
    return max(p.x * 0.866025 + p.y * 0.5, p.y);  // 0 at center .. ~0.5 at edge
}

vec2 hexLocal(vec2 uv) {
    vec2 r = vec2(1.0, 1.732);
    vec2 h = r * 0.5;
    vec2 a = mod(uv, r) - h;
    vec2 b = mod(uv - h, r) - h;
    return dot(a, a) < dot(b, b) ? a : b;
}

#endif
