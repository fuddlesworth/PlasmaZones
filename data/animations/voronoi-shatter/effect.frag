// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Voronoi Shatter transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/voronoi-shatter). Voronoi-
// cell shatter — each shard reveals at its own threshold via per-cell
// hash.
//
// Niri's voronoi-shatter ships symmetric close.glsl/open.glsl.
// PlasmaZones' runtime flips iTime on reverse legs (1→0 on close, 0→1
// on open), so we use the niri OPEN body with `niri_clamped_progress`
// translated to `clamp(iTime, 0.0, 1.0)` and the runtime flip
// auto-mirrors the visual on close — no iIsReversed branch needed. One
// timeline deviation from the verbatim body: see the reveal-band
// comment below.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

vec2 vs_hash2(vec2 p) {
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)),
                           dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    // `cellDensity` means "Voronoi cells across the screen": multiplying
    // by iAnchorSize/iSurfaceScreenPos.zw scales the cell count to the
    // fraction of the screen this surface covers, so shard pixel size
    // stays constant across popup vs. maximized windows. Matches niri's
    // reference on full-screen (multiplier = 1.0 there).
    vec2 scale = vec2(p_cellDensity) * max(iAnchorSize, vec2(1.0))
                                   / max(iSurfaceScreenPos.zw, vec2(1.0));
    vec2 q = uv * scale;
    vec2 g = floor(q);
    vec2 f = fract(q);
    float min_d = 100.0;
    vec2 cell = g;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 nb = vec2(float(x), float(y));
            vec2 r = nb + vs_hash2(g + nb) - f;
            float d = dot(r, r);
            if (d < min_d) { min_d = d; cell = g + nb; }
        }
    }
    float seed = vs_hash2(cell).x;
    // Deviation from the verbatim niri body. Niri layers two smoothsteps:
    // shard_p = smoothstep(seed*0.5, seed*0.5 + spread, p), then
    // reveal = smoothstep(0, softness, shard_p). The outer one saturates
    // once shard_p reaches `softness` — the MIDDLE of the shard's band at
    // the 0.5/0.5 defaults — so the last shard (seed → 1) was fully
    // revealed at p = 0.75 and the final quarter of the leg was a static
    // frame (the phosphor-peek dead-domain bug; flipped, a dead head on
    // close). Collapse to one band per shard: `w` is the effective fade
    // width the old composition produced at the defaults
    // (spread × softness = 0.25), and seeding the thresholds over
    // [0, 1 - w] tiles the bands so their union spans exactly [0, 1] for
    // ANY dial values — the first shards start at p ≈ 0 and the last
    // completes at exactly p = 1.
    float w = clamp(p_revealSpread * p_shardSoftness, 1.0e-3, 1.0);
    float thr = seed * (1.0 - w);
    float reveal = smoothstep(thr, thr + w, p);

    return win * reveal;
}
