// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2021 Simon Schneegans (Burn-My-Windows)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// License rationale: Burn-My-Windows' upstream `matrix.frag` and the
// helper functions copied verbatim from its `common.glsl` (`hash22`,
// `alphaOver`, `getInputColor`) are GPL-3.0-or-later. Combining that
// code with PlasmaZones requires GPL-3.0-or-later for this file
// specifically — it CANNOT be sub-licensed as the LGPL-2.1-or-later
// the rest of `data/animations/` uses. Other animation shaders in this
// directory remain LGPL-2.1-or-later because their authoring is
// PlasmaZones-original; only files with verbatim BMW content carry the
// upstream-compatible GPL-3.0-or-later identifier (see honeycomb's
// niri-port for the same pattern).
//
// Matrix transition — ported from Burn-My-Windows' matrix.frag. The
// rendered surface (`uTexture0`) gets wiped through a cascade of
// falling glyphs sampled from a 16×16 atlas (`uTexture1`,
// `matrix-font.png`). Per-column delays and speeds keep the cascade
// from looking metronomic.
//
// DIRECTION — matrix is ASYMMETRIC: rain physically falls top-to-bottom
// in BOTH directions, but the window-content reveal trajectory inverts
// (open: invisible → visible; close: visible → invisible). So it is
// written as a `pIn`/`pOut` pair: the harness feeds both a forward
// 0→1 `t` (it applies `legProgress()` for us, so the rain always falls
// forward) and dispatches to the right function by leg direction. The
// only difference between the two is `windowFadingIn`, threaded into the
// windowAlpha sweep — no `iIsReversed`, no manual iTime un-flip.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main(). noise.glsl (hash22) is pack-specific, so it stays here.
#include <noise.glsl>

// p_letterSize / p_randomness / p_overshoot (customParams[0].xyz) and
// p_trailColor / p_tipColor (customColors[0..1]) are generated from
// metadata.json — no hand-written slot #defines.

const float EDGE_FADE             = 30.0;
const float FADE_WIDTH            = 150.0;
const float TRAIL_LENGTH          = 0.2;
const float FINAL_FADE_START_TIME = 0.8;
const float LETTER_TILES          = 16.0;
const float LETTER_FLICKER_SPEED  = 2.0;
// BMW's original used `LETTER_FLICKER_SPEED * uProgress * uDuration`
// where the product was elapsed-leg-seconds. Our contract has `iFrame`
// (per-leg frame counter) but no leg-duration uniform, so we scale
// iFrame by an assumed display rate to match BMW's per-frame cycling
// cadence. 60 Hz is the canonical KWin/Qt scene-graph default.
const float ASSUMED_FPS           = 60.0;

// hash22 hosted in shared/noise.glsl. Drives both the per-column
// delay and the in-tile glyph index (so each "drop" cycles through
// glyphs as it falls). Identical formulation to BMW's common.glsl.

// Quad-edge alpha mask: 1.0 in the interior, fading to 0 within
// `fadePixels` of any edge. Keeps the rain from cropping abruptly at
// the window border. Floor `iResolution` so an early-frame surface
// that hasn't reported its size (iResolution.x or .y == 0) doesn't
// produce NaN pixel coords — matches the defensive pattern used by
// doom/hexagon/pixel-wheel/pixel-wipe/aura-glow.
float edgeMask(float fadePixels) {
    vec2 res = resolutionSafe();
    vec2 px = vTexCoord * res;
    vec2 fromEdge = min(px, res - px);
    return clamp(min(fromEdge.x, fromEdge.y) / fadePixels, 0.0, 1.0);
}

// Glyph atlas sample. The atlas is a 16×16 grid of glyphs; each
// fragment picks a tile based on its block coordinates plus a flicker
// offset that cycles glyphs over time. Map iFrame onto BMW's effective
// rate by scaling with `1/ASSUMED_FPS`: increments of
// `LETTER_FLICKER_SPEED / ASSUMED_FPS` per frame land integer crossings
// every ~30 frames at LETTER_FLICKER_SPEED=2 (≈ 2 Hz at 60 fps),
// matching BMW's per-frame cycling rate at the same speed value.
float getText(vec2 fragCoord) {
    // Floor letterSize so a metadata-bypassed value of 0 (or near-zero)
    // doesn't divide-by-zero into NaN texture UVs. Live metadata
    // declares a min of 5.0 so this is defence in depth against a
    // hand-rolled animation engine bypassing schema validation.
    float ls = max(p_letterSize, 1.0);
    vec2 pixelCoords = fragCoord * iResolution;
    vec2 uv          = mod(pixelCoords, ls) / ls;
    vec2 block       = pixelCoords / ls - uv;

    uv += floor(hash22(floor(hash22(block) * vec2(12.9898, 78.233)
                              + LETTER_FLICKER_SPEED * float(iFrame) * (1.0 / ASSUMED_FPS)
                              + 42.254))
                * LETTER_TILES);
    return texture(uTexture1, uv / LETTER_TILES).r;
}

// Per-column drop position. Returns vec2(rainAlpha, windowAlpha):
//   .x = rain trail intensity (0..1, fades exponentially behind head)
//   .y = window content visibility (0 = obscured, 1 = revealed)
//
// `legProgress` is the absolute forward leg progress (0 at leg start,
// 1 at leg end) regardless of direction — the harness already un-flipped
// iTime via legProgress() before calling. `windowFadingIn` is the leg
// direction (true = open/in, false = close/out), which selects the
// windowAlpha trajectory. distToDrop > 0 means the drop head has passed
// this fragment; < 0 means it hasn't reached yet.
vec2 getRain(vec2 fragCoord, float legProgress, bool windowFadingIn) {
    // Floor letterSize for parity with getText — `mod(column, 0)` is
    // undefined / NaN in GLSL; defence in depth against a metadata
    // bypass that pushes letterSize to zero.
    float ls = max(p_letterSize, 1.0);
    // Floor iResolution so a first-frame zero-sized surface doesn't
    // collapse `column` to zero across every fragment — that would
    // synchronize every visible drop to one column for that paint.
    // The sibling helpers (`getText`, `edgeMask`) already use this
    // pattern; do the same here for consistency.
    vec2 res = resolutionSafe();
    float column = fragCoord.x * res.x;
    column -= mod(column, ls);

    // Clamp randomness as defence in depth — metadata declares [0,1]
    // but a host that bypasses validation could push it out of range
    // and turn the per-column delay/speed jitter into NaN territory.
    float r = clamp(p_randomness, 0.0, 1.0);

    // Offset `sin(column)` so the leftmost column (column == 0) gets
    // a non-trivial phase. Without the offset, `sin(0) == 0` makes
    // every window's leftmost column always lead the cascade with
    // zero delay, breaking the per-column staggered look. `cos(column)`
    // doesn't need the same fix because `cos(0) == 1` already produces
    // a non-zero seed.
    float delay = fract(sin(column + 17.0) * 78.233) * r;
    float speed = fract(cos(column) * 12.989) * (r * 0.3) + 1.5;

    // BMW's `uProgress` matches our `legProgress` exactly: 0 at start,
    // 1 at end, in BOTH directions.
    float distToDrop  = (legProgress * 2.0 - delay) * speed - fragCoord.y;

    float rainAlpha   = distToDrop >= 0.0 ? exp(-distToDrop / TRAIL_LENGTH) : 0.0;

    // Window-fadeout sweep: 1 ahead of the rain head (window still
    // visible), 0 behind (window has been wiped). BMW's natural
    // `windowAlpha` for a CLOSE leg.
    float windowSweep = 1.0 - clamp(res.y * distToDrop, 0.0, FADE_WIDTH) / FADE_WIDTH;

    rainAlpha *= edgeMask(EDGE_FADE);

    float shorten =
        fract(sin(column + 42.0) * 33.423) * (r * p_overshoot * 0.25);
    if (shorten > 0.0) {
        rainAlpha *= smoothstep(0.0, 1.0, clamp(fragCoord.y / shorten, 0.0, 1.0));
        rainAlpha *= smoothstep(0.0, 1.0, clamp((1.0 - fragCoord.y) / shorten, 0.0, 1.0));
    }

    // Direction-aware windowAlpha: BMW's CLOSE math gives the trajectory
    // 1 → 0 (visible at start, wiped at end). For OPEN (windowFadingIn),
    // invert so the window fades INTO visibility behind the rain.
    float windowAlpha = windowFadingIn ? (1.0 - windowSweep) : windowSweep;
    return vec2(rainAlpha, windowAlpha);
}

// Source-over composite returning PREMULTIPLIED output. BMW's upstream
// `alphaOver` returns un-premultiplied (their `setOutputColor` macro
// premultiplies on the way out); our pipeline writes `fragColor`
// directly and both runtimes' compositors expect premultiplied input.
// Returning straight-alpha here would produce visible halos around
// semi-transparent rain pixels during the compositor's blend.
//
// Inputs `under` and `over` are still straight-alpha (consistent with
// the rest of the shader's local math); we premultiply at the blend.
vec4 alphaOver(vec4 under, vec4 over) {
    return vec4(over.rgb * over.a + under.rgb * under.a * (1.0 - over.a),
                over.a + under.a * (1.0 - over.a));
}

// Surface sample with un-premultiplied colour. Mirrors BMW's
// `getInputColor`: KWin and the daemon both deliver the surface as
// premultiplied RGBA, so divide-by-alpha before colour math keeps
// blends consistent across runtimes. Threshold matches the rest of the
// suite (glitch, dissolve) — gating on `> 0.001` avoids divide-by-near-
// zero blow-ups at edge-fade pixels where alpha can land in the
// 0..0.001 range and produce rgb >> 1.0.
//
// Off-window guard: the entry remaps `coords.y` through
// `coords.y * (overshoot + 1) - overshoot * 0.5`, which lands outside
// [0, 1] for the overshoot bands at the top/bottom of the surface
// whenever `overshoot > 0`. Without this guard, the clamp-to-edge
// sampler would smear edge texels into the overshoot region and the
// un-premult divide above would amplify their colour. Force off-window
// samples to fully transparent — same pattern as doom's `inside` mask
// — so the overshoot bands compose as empty pixels.
vec4 getInputColor(vec2 coords) {
    if (coords.x < 0.0 || coords.x > 1.0 || coords.y < 0.0 || coords.y > 1.0) {
        return vec4(0.0);
    }
    vec4 color = surfaceColor(coords);
    if (color.a > 0.001) {
        color.rgb /= color.a;
    }
    return color;
}

// Shared body for both legs. `t` is forward 0→1 leg progress (the harness
// un-flipped iTime via legProgress()); `windowFadingIn` selects the
// window-reveal trajectory. `uv` is vTexCoord.
vec4 matrixRain(vec2 uv, float t, bool windowFadingIn) {
    vec2 coords = uv;
    coords.y    = coords.y * (p_overshoot + 1.0) - p_overshoot * 0.5;

    vec2 rainMask  = getRain(coords, t, windowFadingIn);
    float textMask = getText(coords);

    vec4 oColor = getInputColor(coords);
    // Capture the surface's original alpha BEFORE the rain-driven
    // visibility decay below — used to mask the rain to the window's
    // actual content. KWin's OffscreenEffect redirects to
    // `expandedGeometry()`, an FBO that includes the drop-shadow /
    // decoration padding around the window frame. Sampling uTexture0
    // out there returns alpha=0 (transparent shadow region), so
    // multiplying rain alpha by surfaceAlpha clips the rain to where
    // the window actually has content. Daemon overlay surfaces match
    // the QQuickItem size with no shadow padding so this mask is a
    // no-op there (alpha=1 inside the popup).
    float surfaceAlpha = oColor.a;
    oColor.a *= rainMask.y;

    // Final-fade: in the last 20% of the leg the residual rain trail
    // fades out so the resolved frame is pure window content with no
    // lingering pixels. Drives off `t` so it ramps in as the transition
    // nears completion regardless of direction.
    float finalFade =
        1.0 - clamp((t - FINAL_FADE_START_TIME) / (1.0 - FINAL_FADE_START_TIME), 0.0, 1.0);
    float rainAlpha = finalFade * rainMask.x;

    vec4 text = vec4(mix(p_trailColor.rgb, p_tipColor.rgb,
                         min(1.0, pow(rainAlpha + 0.1, 4.0))),
                     rainAlpha * textMask * surfaceAlpha);

    return alphaOver(oColor, text);
}

vec4 pIn(vec2 uv, float t)  { return matrixRain(uv, t, true);  }
vec4 pOut(vec2 uv, float t) { return matrixRain(uv, t, false); }
