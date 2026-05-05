// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2021 Simon Schneegans (Burn-My-Windows)
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Matrix transition — ported from Burn-My-Windows' matrix.frag. The
// rendered surface (`uTexture0`) gets wiped through a cascade of
// falling glyphs sampled from a 16×16 atlas (`uTexture1`,
// `matrix-font.png`). Per-column delays and speeds keep the cascade
// from looking metronomic.
//
// Visual deviation from BMW: BMW renders rain top-to-bottom for both
// open and close because they pass an explicit `uForOpening` uniform
// that flips windowAlpha while keeping the rain motion forward. Our
// contract drives a single direction-aware `iTime` (the C++ side flips
// it for "going-away" events via the per-transition `reverse` flag),
// which lets symmetric shaders auto-mirror but means asymmetric
// shaders like Matrix observe the rain reversing direction across
// open/close. The visual still reads as "appear via cascade" / "vanish
// via cascade"; the rain just walks bottom-to-top on open and top-to-
// bottom on close. Adding a separate forward-progress uniform to the
// contract is the future fix if this becomes objectionable.

#version 450

#include <animation_uniforms.glsl>

// metadata.json declaration order → customParams[0] sub-slots.
#define letterSize customParams[0].x
#define randomness customParams[0].y
#define overshoot  customParams[0].z

// metadata.json color declaration order → customColors[0..1].
#define trailColor customColors[0]
#define tipColor   customColors[1]

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

const float EDGE_FADE             = 30.0;
const float FADE_WIDTH            = 150.0;
const float TRAIL_LENGTH          = 0.2;
const float FINAL_FADE_START_TIME = 0.8;
const float LETTER_TILES          = 16.0;
const float LETTER_FLICKER_SPEED  = 2.0;

// hash22 — Burn-My-Windows common.glsl. Drives both the per-column
// delay and the in-tile glyph index (so each "drop" cycles through
// glyphs as it falls).
vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

// Quad-edge alpha mask: 1.0 in the interior, fading to 0 within
// `fadePixels` of any edge. Keeps the rain from cropping abruptly at
// the window border.
float edgeMask(float fadePixels) {
    vec2 px = vTexCoord * iResolution;
    vec2 fromEdge = min(px, iResolution - px);
    return clamp(min(fromEdge.x, fromEdge.y) / fadePixels, 0.0, 1.0);
}

// Glyph atlas sample. The atlas is a 16×16 grid of glyphs; each
// fragment picks a tile based on its block coordinates plus a flicker
// offset that cycles glyphs over time.
float getText(vec2 fragCoord) {
    vec2 pixelCoords = fragCoord * iResolution;
    vec2 uv          = mod(pixelCoords, letterSize) / letterSize;
    vec2 block       = pixelCoords / letterSize - uv;

    uv += floor(hash22(floor(hash22(block) * vec2(12.9898, 78.233)
                              + LETTER_FLICKER_SPEED * (1.0 - iTime) * iTimeDelta * 60.0
                              + 42.254))
                * LETTER_TILES);
    return texture(uTexture1, uv / LETTER_TILES).r;
}

// Per-column drop position. Returns vec2(rainAlpha, windowAlpha):
//   .x = rain trail intensity (0..1, fades exponentially behind head)
//   .y = window content visibility (0 = obscured, 1 = revealed)
//
// `bmwProgress = 1.0 - iTime` maps our `iTime ∈ [0,1]` (0 = transition
// start, 1 = fully resolved) onto BMW's progress semantic (0 = visible,
// 1 = transition complete). distToDrop > 0 means the drop head has
// already passed this fragment; < 0 means it hasn't reached yet.
vec2 getRain(vec2 fragCoord) {
    float column = fragCoord.x * iResolution.x;
    column -= mod(column, letterSize);

    float delay = fract(sin(column) * 78.233) * mix(0.0, 1.0, randomness);
    float speed = fract(cos(column) * 12.989) * mix(0.0, 0.3, randomness) + 1.5;

    float bmwProgress = 1.0 - iTime;
    float distToDrop  = (bmwProgress * 2.0 - delay) * speed - fragCoord.y;

    float rainAlpha   = distToDrop >= 0.0 ? exp(-distToDrop / TRAIL_LENGTH) : 0.0;
    float windowAlpha = 1.0 - clamp(iResolution.y * distToDrop, 0.0, FADE_WIDTH) / FADE_WIDTH;

    rainAlpha *= edgeMask(EDGE_FADE);

    float shorten =
        fract(sin(column + 42.0) * 33.423) * mix(0.0, overshoot * 0.25, randomness);
    if (shorten > 0.0) {
        rainAlpha *= smoothstep(0.0, 1.0, clamp(fragCoord.y / shorten, 0.0, 1.0));
        rainAlpha *= smoothstep(0.0, 1.0, clamp((1.0 - fragCoord.y) / shorten, 0.0, 1.0));
    }

    // BMW's `if (uForOpening) windowAlpha = 1.0 - windowAlpha;` is
    // unnecessary here: our `reverse` flag flips iTime upstream so the
    // bmwProgress trajectory inverts itself for the close direction.
    return vec2(rainAlpha, windowAlpha);
}

// Alpha-over composite — Burn-My-Windows common.glsl alphaOver.
// Authors keep this verbatim so the colour math matches the original.
vec4 alphaOver(vec4 under, vec4 over) {
    if (under.a == 0.0 && over.a == 0.0) {
        return vec4(0.0);
    }
    float a = mix(under.a, 1.0, over.a);
    return vec4(mix(under.rgb * under.a, over.rgb, over.a) / a, a);
}

// Surface sample with un-premultiplied colour. Mirrors BMW's
// `getInputColor`: KWin and the daemon both deliver the surface as
// premultiplied RGBA, so divide-by-alpha before colour math keeps
// blends consistent across runtimes.
vec4 getInputColor(vec2 coords) {
    vec4 color = texture(uTexture0, coords);
    if (color.a > 0.0) {
        color.rgb /= color.a;
    }
    return color;
}

void main() {
    vec2 coords = vTexCoord;
    coords.y    = coords.y * (overshoot + 1.0) - overshoot * 0.5;

    vec2 rainMask  = getRain(coords);
    float textMask = getText(coords);

    vec4 oColor = getInputColor(coords);
    oColor.a *= rainMask.y;

    // Final-fade: in the last 20% of the leg the residual rain trail
    // fades out so the resolved frame is pure window content with no
    // lingering pixels. With our iTime contract (1 = fully resolved)
    // we drive the fade off `1.0 - iTime` so it ramps in as the
    // transition nears completion.
    float bmwProgress = 1.0 - iTime;
    float finalFade   =
        1.0 - clamp((bmwProgress - FINAL_FADE_START_TIME) / (1.0 - FINAL_FADE_START_TIME), 0.0, 1.0);
    float rainAlpha = finalFade * rainMask.x;

    vec4 text = vec4(mix(trailColor.rgb, tipColor.rgb,
                         min(1.0, pow(rainAlpha + 0.1, 4.0))),
                     rainAlpha * textMask);

    oColor    = alphaOver(oColor, text);
    fragColor = oColor;
}
