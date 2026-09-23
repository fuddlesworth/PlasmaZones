// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Opt-in colour-space helpers for SURFACE shader packs. `#include
// <surface_color.glsl>` only in packs that need them (hue-cycling borders,
// perceptual saturation). The sRGB <-> linear <-> OKLab suite is the canonical
// Ottosson reference implementation (public); hsv2rgb is the standard hue-wheel
// form. Both are generic colour science, not pack-specific look code.

#ifndef PLASMAZONES_SURFACE_COLOR_GLSL
#define PLASMAZONES_SURFACE_COLOR_GLSL

// Rec.709 relative-luminance weights (the sRGB primaries' Y coefficients).
float luma709(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Rec.601 luma weights (the classic NTSC greyscale mix). A different convention
// from luma709 on purpose — packs pick the one their look was tuned against.
float luma601(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// HSV -> RGB (h,s,v in [0,1]).
vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

// sRGB <-> linear.
vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}
vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

// linear <-> OKLab (Ottosson reference matrices).
vec3 linearToOklab(vec3 c) {
    float l = 0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b;
    float m = 0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b;
    float s = 0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b;
    float l_ = pow(max(l, 0.0), 1.0 / 3.0);
    float m_ = pow(max(m, 0.0), 1.0 / 3.0);
    float s_ = pow(max(s, 0.0), 1.0 / 3.0);
    return vec3(0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
                1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
                0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_);
}
vec3 oklabToLinear(vec3 c) {
    float l_ = c.r + 0.3963377774 * c.g + 0.2158037573 * c.b;
    float m_ = c.r - 0.1055613458 * c.g - 0.0638541728 * c.b;
    float s_ = c.r - 0.0894841775 * c.g - 1.2914855480 * c.b;
    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;
    return vec3(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);
}

// Perceptual saturation in OKLab (a,b scaled by `saturation`); identity at 1.0.
vec3 oklabSaturate(vec3 srgb, float saturation) {
    if (abs(saturation - 1.0) < 0.001) {
        return srgb;
    }
    vec3 lab = linearToOklab(srgbToLinear(clamp(srgb, 0.0, 1.0)));
    lab.gb *= saturation;
    return linearToSrgb(clamp(oklabToLinear(lab), 0.0, 1.0));
}

// Brightness / contrast / saturation on an UN-premultiplied sRGB colour, the
// three knobs a blur-behind pane exposes. Identity at (1, 1, 1). Brightness is
// a plain gain, contrast pivots on mid-grey, and saturation is perceptual
// (OKLab) so pushing it does not shift hue. A premultiplied caller divides by
// alpha first and re-multiplies after.
vec3 surfaceColorAdjust(vec3 c, float brightness, float contrast, float saturation) {
    c = clamp(c * max(brightness, 0.0), 0.0, 1.0);
    c = clamp((c - 0.5) * max(contrast, 0.0) + 0.5, 0.0, 1.0);
    return oklabSaturate(c, max(saturation, 0.0));
}

// ── Vibrancy ────────────────────────────────────────────────────────────────
// Hyprland's blur vibrancy (src/render/shaders/glsl/blur1.glsl, BSD-3), the
// livelier cousin of a flat saturation gain: it boosts saturation only where
// a colour is already both saturated and perceptually bright, so a muted
// wallpaper stays muted and deep blues are not pushed as hard as an equally
// saturated yellow. `vibrancy` is the boost (0 off, Hyprland's default is
// 0.1696); `darkness` in 0..1 lets darker colours receive the boost too.
// Hyprland divides the boost by its pass count because it applies this in
// every down pass; here it runs once on the finished blur, so it is applied
// whole. sRGB in, sRGB out, un-premultiplied.

// HSP perceived-brightness weights (alienryderflex.com/hsp.html).
const vec3 kSurfaceVibrancyHsp = vec3(0.299, 0.587, 0.114);

// Two quarter-circle arcs joined at `a` (flong.com shapers_circ): the curve
// that maps perceived brightness onto "how much does this colour deserve".
float surfaceDoubleCircleSigmoid(float x, float a) {
    a = clamp(a, 0.0, 1.0);
    if (x <= a) {
        return a - sqrt(max(a * a - x * x, 0.0));
    }
    return a + sqrt(max((1.0 - a) * (1.0 - a) - (x - 1.0) * (x - 1.0), 0.0));
}

vec3 surfaceRgb2hsl(vec3 col) {
    float minc = min(col.r, min(col.g, col.b));
    float maxc = max(col.r, max(col.g, col.b));
    float delta = maxc - minc;
    float lum = (minc + maxc) * 0.5;
    float sat = 0.0;
    float hue = 0.0;
    if (lum > 0.0 && lum < 1.0) {
        float mul = (lum < 0.5) ? lum : (1.0 - lum);
        sat = delta / (mul * 2.0);
    }
    if (delta > 0.0) {
        vec3 masks = vec3(equal(vec3(maxc), col)) * vec3(notEqual(vec3(maxc), vec3(col.g, col.b, col.r)));
        vec3 adds = vec3(0.0, 2.0, 4.0) + vec3(col.g - col.b, col.b - col.r, col.r - col.g) / delta;
        hue += dot(adds, masks);
        hue /= 6.0;
        if (hue < 0.0) {
            hue += 1.0;
        }
    }
    return vec3(hue, sat, lum);
}

vec3 surfaceHsl2rgb(vec3 col) {
    const float onethird = 1.0 / 3.0;
    const float twothird = 2.0 / 3.0;
    float hue = col.x;
    float sat = col.y;
    float lum = col.z;
    vec3 xt;
    if (hue < onethird) {
        xt = vec3(6.0 * (onethird - hue), 6.0 * hue, 0.0);
    } else if (hue < twothird) {
        xt = vec3(0.0, 6.0 * (twothird - hue), 6.0 * (hue - onethird));
    } else {
        xt = vec3(6.0 * (hue - twothird), 0.0, 6.0 * (1.0 - hue));
    }
    xt = min(xt, 1.0);
    vec3 ct = (2.0 * sat * xt) + (1.0 - sat);
    if (lum >= 0.5) {
        return ((1.0 - lum) * ct) + (2.0 * lum - 1.0);
    }
    return lum * ct;
}

vec3 surfaceVibrancy(vec3 color, float vibrancy, float darkness) {
    if (vibrancy <= 0.0) {
        return color;
    }
    // Hyprland's constants: a and b weight saturation against brightness in
    // the boost gate, c is the softness of the gate's edge.
    const float a = 0.93;
    const float b = 0.11;
    const float c = 0.66;
    float darkness1 = 1.0 - clamp(darkness, 0.0, 1.0);
    vec3 hsl = surfaceRgb2hsl(clamp(color, 0.0, 1.0));
    float perceived = surfaceDoubleCircleSigmoid(sqrt(dot(color * color, kSurfaceVibrancyHsp)), 0.8 * darkness1);
    float b1 = b * darkness1;
    float gate = 1.0 - (pow(1.0 - hsl.y * cos(a), 2.0) + pow(1.0 - perceived * sin(a), 2.0));
    float boostBase = hsl.y > 0.0 ? smoothstep(b1 - c * 0.5, b1 + c * 0.5, gate) : 0.0;
    float saturation = clamp(hsl.y + boostBase * vibrancy, 0.0, 1.0);
    return surfaceHsl2rgb(vec3(hsl.x, saturation, hsl.z));
}

// The blur family's shared colour grade on a PREMULTIPLIED backdrop sample:
// brightness, contrast, OKLab saturation and vibrancy, un-premultiplied for
// the maths and re-premultiplied by the sample's own alpha. Identity at
// (1, 1, 1, 0, *).
vec4 surfaceBackdropGrade(vec4 premul, float brightness, float contrast, float saturation, float vibrancy,
                          float vibrancyDarkness) {
    if (premul.a <= 0.001) {
        return premul;
    }
    vec3 c = premul.rgb / premul.a;
    c = surfaceColorAdjust(c, brightness, contrast, saturation);
    c = surfaceVibrancy(c, vibrancy, vibrancyDarkness);
    return vec4(clamp(c, 0.0, 1.0) * premul.a, premul.a);
}

#endif // PLASMAZONES_SURFACE_COLOR_GLSL
