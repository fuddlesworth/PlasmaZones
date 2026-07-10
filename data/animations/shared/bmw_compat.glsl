// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-FileCopyrightText: 2021-2024 Simon Schneegans (Burn-My-Windows)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Burn-My-Windows shader compatibility shim. PlasmaZones' BMW-derived
// animation shaders include this header to import BMW's helper
// functions (alphaOver, getInputColor, easing curves, simplex3D, etc.)
// and to remap BMW's uniform names onto the PlasmaZones runtime
// contract.
//
// License: GPL-3.0-or-later. BMW's `resources/shaders/common.glsl` is
// GPL-3.0-or-later; the helpers below are byte-equivalent ports of
// that file's bodies. Any animation shader that includes this file
// inherits the GPL-3.0-or-later identifier — see `matrix/effect.frag`
// for the same convention applied to a single shader file.
//
// Uniform name remap (BMW → PlasmaZones):
//   • `iTexCoord` → `vTexCoord`. (KWin's vertex stage already applies
//     the Y=0-at-top flip BMW expects; see animation_uniforms.glsl
//     header for details.)
//   • `uForOpening` → `(iIsReversed == 0)`. BMW sets `uForOpening` on
//     the OPEN leg; PlasmaZones' `iIsReversed` is 0 on the open leg
//     and non-zero on the close leg — opposite sense, so flipped.
//   • `uProgress` → leg-progress 0→1 recovered from iTime. PlasmaZones
//     flips iTime on the close leg (1→0), so absolute leg progress is
//     `mix(iTime, 1 - iTime, float(iIsReversed))` clamped to [0, 1].
//   • `uSize` → `max(iAnchorSize, vec2(1.0))` (defends against the
//     early-frame zero-size case `iAnchorSize` can briefly carry).
//   • `uPadding` → `0.0` (PlasmaZones doesn't padding-shadow surfaces;
//     the kwin-effect's redirected window content has no shadow
//     border).
//   • `uIsFullscreen` → `false` (PlasmaZones doesn't track per-surface
//     fullscreen state; BMW's edge-mask shortcut on fullscreen
//     windows just degrades into the regular per-edge fade here).
//   • `uProgress * uDuration` (BMW elapsed-seconds idiom) → substitute
//     manually in body code with `float(iFrame) / 60.0`. PlasmaZones
//     has no per-leg duration uniform; `iFrame` at the 60 Hz Qt
//     scene-graph default gives BMW-equivalent elapsed seconds.
//
// Color contract: BMW shaders work in straight-alpha space and only
// premultiply at the very end (in `setOutputColor`). PlasmaZones'
// `uTexture0` is premultiplied. `getInputColor` here un-premultiplies
// the sample so BMW body code stays 1:1 verbatim against upstream;
// `setOutputColor` re-premultiplies before writing to `fragColor`.
//
// Caller contract: include `<animation_uniforms.glsl>` BEFORE this
// header so `iIsReversed`, `iTime`, `iFrame`, `iAnchorSize`, and
// `uTexture0` are in scope. The consuming shader must declare
// `layout(location = 0) in vec2 vTexCoord;` and
// `layout(location = 0) out vec4 fragColor;` BEFORE this include for
// `iTexCoord` and `setOutputColor` to compile.
//
// This file includes `<noise.glsl>` directly (see the include below), so
// its `hash22`, `simplex2D`, `simplex2DFractal`, and `hash12` are in scope
// for every BMW shader without a separate include — those are byte-equivalent
// independent ports of the same Inigo Quilez patterns, and `hash12` in
// particular is now sourced from noise.glsl rather than duplicated here. The
// combined-work license stays GPL-3.0-or-later because this file also pulls
// in BMW-derived bodies.
//
// Hash coverage: only `hash11` and `hash33` (the inputs simplex3D needs) are
// mirrored from BMW here. The other hashNN variants in BMW's common.glsl
// (`hash13`, `hash21`, `hash23`, `hash31`, `hash32`, `hash41..44`) are not
// ported — a future shader that needs one keeps its own file-scope copy until
// enough consumers justify lifting it.

#ifndef PHOSPHOR_BMW_COMPAT_GLSL
#define PHOSPHOR_BMW_COMPAT_GLSL

// Shared LGPL primitives this shim builds on: `hash12`, `hash22`, and the
// simplex-noise helpers live in noise.glsl; the four easing curves live in
// easing.glsl. Including both here means every BMW shader gets them without a
// separate include (noise.glsl's guard makes a redundant include harmless). A
// GPL work including LGPL headers is fine.
#include <noise.glsl>
#include <easing.glsl>

// ── Uniform-name aliases ─────────────────────────────────────────────
#define iTexCoord     vTexCoord
#define uForOpening   (iIsReversed == 0)
// Leg-progress 0→1 recovered from iTime. The two `clamp(iTime, 0, 1)`
// inputs to `mix` already guarantee the result is in [0, 1] for any
// finite `iIsReversed`, so no outer clamp is needed.
#define uProgress     mix(clamp(iTime, 0.0, 1.0), 1.0 - clamp(iTime, 0.0, 1.0), float(iIsReversed))
#define uSize         max(iAnchorSize, vec2(1.0))
#define uPadding      (0.0)
#define uIsFullscreen (false)

// ── I/O bridges (premul ↔ straight-alpha) ───────────────────────────
// Reads the surface through `surfaceColor()` (from animation_uniforms.glsl,
// which the caller contract above requires be included first) so the
// kwin-path Y-flip is applied — a raw `texture(uTexture0, ...)` here
// would render the surface upside-down on the compositor path.
vec4 getInputColor(vec2 coords) {
    vec4 color = surfaceColor(coords);
    if (color.a > 0.0) {
        color.rgb /= color.a;
    }
    return color;
}

void setOutputColor(vec4 outColor) {
    fragColor = vec4(outColor.rgb * outColor.a, outColor.a);
}

// ── Composition: BMW common.glsl:189 verbatim ───────────────────────
vec4 alphaOver(vec4 under, vec4 over) {
    if (under.a == 0.0 && over.a == 0.0) {
        return vec4(0.0);
    }
    float alpha = mix(under.a, 1.0, over.a);
    return vec4(mix(under.rgb * under.a, over.rgb, over.a) / alpha, alpha);
}

// ── Easing curves ────────────────────────────────────────────────────
// easeOutQuad / easeInQuad / easeOutCubic / easeInOutCubic now live in the
// shared LGPL easing.glsl (included above); the canonical Penner bodies
// (the same forms BMW uses) are unchanged there.

// ── Hue rotation: BMW common.glsl:218 verbatim ──────────────────────
vec3 offsetHue(vec3 color, float hueOffset) {
    float maxC  = max(max(color.r, color.g), color.b);
    float minC  = min(min(color.r, color.g), color.b);
    float delta = maxC - minC;
    float hue   = 0.0;
    if (delta > 0.0) {
        if (maxC == color.r) {
            hue = mod((color.g - color.b) / delta, 6.0);
        } else if (maxC == color.g) {
            hue = (color.b - color.r) / delta + 2.0;
        } else {
            hue = (color.r - color.g) / delta + 4.0;
        }
    }
    hue /= 6.0;
    float saturation = (maxC > 0.0) ? (delta / maxC) : 0.0;
    float value      = maxC;
    hue = mod(hue + hueOffset, 1.0);
    float c = value * saturation;
    float x = c * (1.0 - abs(mod(hue * 6.0, 2.0) - 1.0));
    float m = value - c;
    vec3 rgb;
    if (hue < 1.0 / 6.0) {
        rgb = vec3(c, x, 0.0);
    } else if (hue < 2.0 / 6.0) {
        rgb = vec3(x, c, 0.0);
    } else if (hue < 3.0 / 6.0) {
        rgb = vec3(0.0, c, x);
    } else if (hue < 4.0 / 6.0) {
        rgb = vec3(0.0, x, c);
    } else if (hue < 5.0 / 6.0) {
        rgb = vec3(x, 0.0, c);
    } else {
        rgb = vec3(c, 0.0, x);
    }
    return rgb + m;
}

// ── 2D rotation: BMW common.glsl:460 verbatim ───────────────────────
vec2 rotate(vec2 a, float angle) {
    return vec2(a.x * cos(angle) - a.y * sin(angle),
                a.x * sin(angle) + a.y * cos(angle));
}
vec2 rotate(vec2 a, float angle, vec2 center) {
    return vec2(cos(angle) * (a.x - center.x) + sin(angle) * (a.y - center.y) + center.x,
                cos(angle) * (a.y - center.y) - sin(angle) * (a.x - center.x) + center.y);
}

// ── hash11: BMW common.glsl:481 verbatim ────────────────────────────
float hash11(float p) {
    p = fract(p * .1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

// ── hash33 (used by simplex3D below): BMW common.glsl:538 verbatim ──
vec3 hash33(vec3 p3) {
    p3 = fract(p3 * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}

// ── simplex3D: BMW common.glsl:609 verbatim ─────────────────────────
float simplex3D(vec3 p) {
    const float F3 = 0.3333333;
    const float G3 = 0.1666667;
    vec3 s = floor(p + dot(p, vec3(F3)));
    vec3 x = p - s + dot(s, vec3(G3));
    vec3 e  = step(vec3(0.0), x - x.yzx);
    vec3 i1 = e * (1.0 - e.zxy);
    vec3 i2 = 1.0 - e.zxy * (1.0 - e);
    vec3 x1 = x - i1 + G3;
    vec3 x2 = x - i2 + 2.0 * G3;
    vec3 x3 = x - 1.0 + 3.0 * G3;
    vec4 w, d;
    w.x = dot(x,  x);
    w.y = dot(x1, x1);
    w.z = dot(x2, x2);
    w.w = dot(x3, x3);
    w = max(0.6 - w, 0.0);
    d.x = dot(-0.5 + hash33(s),       x);
    d.y = dot(-0.5 + hash33(s + i1),  x1);
    d.z = dot(-0.5 + hash33(s + i2),  x2);
    d.w = dot(-0.5 + hash33(s + 1.0), x3);
    w *= w;
    w *= w;
    d *= w;
    return dot(d, vec4(52.0)) * 0.5 + 0.5;
}

// ── simplex3DFractal: BMW common.glsl:661 verbatim ──────────────────
float simplex3DFractal(vec3 m) {
    const mat3 rot1 = mat3(-0.37,  0.36,  0.85,
                           -0.14, -0.93,  0.34,
                            0.92,  0.01,  0.40);
    const mat3 rot2 = mat3(-0.55, -0.39,  0.74,
                            0.33, -0.91, -0.24,
                            0.77,  0.12,  0.63);
    const mat3 rot3 = mat3(-0.71,  0.52, -0.47,
                           -0.08, -0.72, -0.68,
                           -0.70, -0.45,  0.56);
    return 0.5333333 * simplex3D(m * rot1) +
           0.2666667 * simplex3D(2.0 * m * rot2) +
           0.1333333 * simplex3D(4.0 * m * rot3) +
           0.0666667 * simplex3D(8.0 * m);
}

// ── hash12 ──────────────────────────────────────────────────────────
// hash12 (BMW's IQ-style `fract(p3 * 0.1031)` chain, byte-equivalent to
// BMW common.glsl:489) now lives in the shared LGPL noise.glsl, included
// above. BMW shaders that need it (tv-glitch, wisps) resolve it from there.

// ── tritone: BMW common.glsl:201 verbatim ───────────────────────────
// Maps val ∈ [0, 1] to a 3-color gradient (shadows → midtones →
// highlights). Used by incinerate and wisps for fire/wisp colour
// ramps; lifted to remove the per-shader duplicates.
vec3 tritone(float val, vec3 shadows, vec3 midtones, vec3 highlights) {
    if (val < 0.5) {
        return mix(shadows, midtones, smoothstep(0.0, 1.0, val * 2.0));
    }
    return mix(midtones, highlights, smoothstep(0.0, 1.0, val * 2.0 - 1.0));
}

// ── Off-window-clipping variant of getInputColor ────────────────────
// Returns vec4(0) when coords falls outside [0, 1] before delegating
// to getInputColor. Used by broken-glass (where the BMW actor-scale
// math pushes shard sample UVs past the anchor's bounds) and
// tv-glitch (where the vertical scale offsets sampled rows past the
// bottom). Both ports previously defined this helper inline; lifted
// here for DRY. The hard clip is intentional — bmw_compat's
// getInputColor reads premultiplied uTexture0 and a clamp-to-edge
// sampler would smear transparent edge pixels onto displaced
// fragments. Soft-mask alternatives (see noise.glsl::boundaryMask)
// do the same job with an antialiased edge for shaders that prefer
// a smooth crop.
vec4 getClippedInputColor(vec2 coords) {
    if (coords.x < 0.0 || coords.x > 1.0 || coords.y < 0.0 || coords.y > 1.0) {
        return vec4(0.0);
    }
    return getInputColor(coords);
}

// ── Edge masks: BMW common.glsl:403, 423, 432 verbatim ──────────────
float getEdgeMask(vec2 uv, vec2 maxUV, float fadeWidth) {
    float mask = 1.0;
    if (!uIsFullscreen) {
        mask *= smoothstep(0.0, 1.0, clamp(uv.x / fadeWidth, 0.0, 1.0));
        mask *= smoothstep(0.0, 1.0, clamp(uv.y / fadeWidth, 0.0, 1.0));
        mask *= smoothstep(0.0, 1.0, clamp((maxUV.x - uv.x) / fadeWidth, 0.0, 1.0));
        mask *= smoothstep(0.0, 1.0, clamp((maxUV.y - uv.y) / fadeWidth, 0.0, 1.0));
    }
    return mask;
}

float getAbsoluteEdgeMask(float fadePixels, float offset) {
    float padding = max(0.0, uPadding - fadePixels * offset);
    vec2 uv       = iTexCoord.st * uSize - padding;
    return getEdgeMask(uv, uSize - 2.0 * padding, fadePixels);
}

float getRelativeEdgeMask(float fadeAmount) {
    vec2 uv = iTexCoord.st;
    return getEdgeMask(uv, vec2(1.0), fadeAmount);
}

#endif // PHOSPHOR_BMW_COMPAT_GLSL
