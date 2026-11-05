// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Voronoi Shatter transition — a Voronoi-cell shatter where each shard
// reveals at its own threshold via a per-cell hash. Inspired by
// liixini/shaders' niri voronoi-shatter shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. One
// timeline detail differs; see the reveal-band comment below.
//
// Geometry and texture coordinates coincide here, so
// `texture(uTexture0, uv)` samples directly.

vec2 vs_hash2(vec2 p) {
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)),
                           dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

vec4 pTransition(vec2 uv, float t) {
    float p = clamp(iTime, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    // `cellDensity` means "Voronoi cells across the screen": multiplying
    // by iAnchorSize/iSurfaceScreenPos.zw scales the cell count to the
    // fraction of the screen this surface covers, so shard pixel size
    // stays constant across popup vs. maximized windows. The multiplier
    // is 1.0 when the surface fills the screen.
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
    // A two-smoothstep layering would set
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
