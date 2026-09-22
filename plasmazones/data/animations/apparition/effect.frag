// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Apparition — window violently sucks into a swirling void. Three
// stacked UV-displacement effects (shake jitter, radial suction, and
// position-dependent whirl rotation) combine with a linear alpha
// envelope to dissolve the window. Visually inspired by the
// equivalent effect in Burn-My-Windows (apparition.frag, Simon
// Schneegans), but written natively against our `iTime`/`uTexture0`
// contract: no `uForOpening` collapse, no compat layer.
//
// ## iTime convention
//
// SurfaceAnimator runs `iTime` 0→1 on show and 1→0 on hide. We
// derive a `progress` that's 0 at the visible state and 1 at the
// destroyed state:
//
//   `progress = 1.0 - clamp(iTime, 0, 1)`
//
// Every distortion magnitude scales with `progress`, so on show
// (iTime 0→1) all three distortions wind down to zero — window
// "un-distorts" into solid form. On hide (iTime 1→0) distortions
// wind up — window dissolves via increasing distortion. This
// matches BMW's open/close behaviour exactly without a direction
// branch, because BMW's only direction difference IS this same
// time reversal.
//
// ## Coordinate space note
//
// BMW shaders run on a doubled-size padded actor (ACTOR_SCALE=2
// with PADDING=0.5) so distorted UV lookups can sample into the
// shadow padding region around the window. We don't have actor
// padding — `uTexture0` covers exactly the surface bounds. So the
// padding-coord-frame math from BMW (`coords = uv * 2 - 0.5 -
// center`) collapses to `coords = uv - center` here, and we force
// off-window samples to transparent at the end so the "blow out
// into void" reading is preserved (without explicit clamping the
// edge-clamp sampler would smear edge texels across the window).
//
// `uTexture0` carries premultiplied alpha (Qt RHI / KWin
// convention). Multiplying the final sample by `visibility` scales
// both RGB and alpha consistently — equivalent to BMW's straight-
// alpha `oColor.a *= 1.0 - progress` followed by setOutputColor
// premultiplication.

// Whirl: rotate `coords` around its origin by an angle that grows
// quadratically toward the centre. Inner points rotate strongly,
// edge points (length≥1) rotate not at all. This is the same math
// BMW use; the warping=1 case rotates a unit-distance point by
// `rotation` rad and a length=0 point by `warping + rotation`.
vec2 whirl(vec2 coords, float warping, float rotation)
{
    float t = max(1.0 - length(coords), 0.0);
    float angle = t * t * warping + rotation;
    float s = sin(angle);
    float c = cos(angle);
    return vec2(dot(coords, vec2(c, -s)), dot(coords, vec2(s, c)));
}

vec4 pTransition(vec2 uv, float t)
{
    // Visibility 1 at fully solid, 0 at fully gone. `progress` is
    // its complement and drives every distortion magnitude.
    float visibility = clamp(t, 0.0, 1.0);
    float progress   = 1.0 - visibility;

    // Suction centre: at randomness=0 the centre is the window
    // middle (consistent across all windows); at randomness=1 the
    // centre is at (seedX, seedY) — useful for an off-centre
    // pinch. Default randomness=0.5 nudges the centre slightly
    // toward (seedX, seedY) without going fully off-centre.
    vec2 seed   = vec2(p_seedX, p_seedY);
    vec2 center = mix(vec2(0.5), seed, clamp(p_randomness, 0.0, 1.0));

    // Coords relative to the suction centre, in UV space.
    vec2 coords = uv - center;

    // Sinusoidal shake. Jitter frequency is tied to `shake` itself
    // so cranking shake up doesn't just amplify but also speeds up
    // the wobble — this is what gives the effect its "rattly" feel
    // rather than a uniform sway. Both axes use the same template
    // (sin on x, cos on y) with the seed coords as phase offsets.
    coords.x += progress * 0.05 * p_shake * sin((progress + seed.x) * (1.0 + seed.x) * p_shake);
    coords.y += progress * 0.05 * p_shake * cos((progress + seed.y) * (1.0 + seed.y) * p_shake);

    // Suction: push sample position outward from centre proportional
    // to progress. The push direction is `coords / (length/sqrt(2))
    // = sqrt(2) · normalize(coords)`, so the magnitude added is
    // `progress * 0.5 * sqrt(2) * suction ≈ 0.707 * progress *
    // suction`. At progress=1 / suction=1 that's enough to push
    // every window-content texel past the original window bounds
    // (window-content radius from centre is at most ≈ 0.71 in our
    // unit-square UV). The visual reads as "content blows outward
    // and exits the window".
    float dist = length(coords) / sqrt(2.0);
    if (dist > 0.0001) {
        coords += progress * (coords / dist) * 0.5 * p_suction;
    }

    // Whirl: rotate the displaced coords around the suction centre.
    // Inner points (close to the centre after suction) rotate
    // strongly, outer points barely. Combined with the suction-
    // outward push, the visual feel is "window content spirals as
    // it gets sucked out of the window".
    coords = whirl(coords, p_twirl * progress, 0.0);

    // Sample. Without explicit clipping the edge-clamp sampler
    // would smear the texture edge across off-window UVs as the
    // suction pushes lookups past [0,1]. Forcing those samples to
    // transparent gives the BMW "into the void" reading.
    vec2 sampleUV  = coords + center;
    vec2 inside    = step(vec2(0.0), sampleUV) * step(sampleUV, vec2(1.0));
    float onScreen = inside.x * inside.y;
    vec4 sampled   = surfaceColor(sampleUV) * onScreen;

    // Alpha envelope: linear in iTime. Keeping this linear (rather
    // than smootherstepping) means the suction/whirl amplitudes and
    // alpha decay together at the same rate, which is what BMW does
    // and what makes the dissolve feel coupled to the distortion.
    return sampled * visibility;
}
