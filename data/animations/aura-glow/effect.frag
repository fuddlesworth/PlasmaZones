// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Aura Glow — window crops into a colour-shifting glowing orb that
// fades. A `|x|^p + |y|^p` super-ellipse gradient (with `p`
// growing from 2 to 100 with progress) blends from a square-shaped
// crop to a circular crop; a hue-cycling rim sits on top, with a
// subtle radial blur on the window content. Visually inspired by
// Burn-My-Windows (aura-glow.frag, Justin Garza + Simon
// Schneegans), written natively against our `iTime`/`uTexture0`.
//
// ## iTime convention
//
// `progress = clamp(iTime, 0, 1)` — 1 at visible, 0 at destroyed.
// (Aura-Glow is one of the rare BMW shaders that uses
// progress=visibility natively, matching our iTime convention
// directly.) On show (iTime 0→1) the window emerges from a
// shrinking circle of glow; on hide (iTime 1→0) it crops back into
// one. No direction branch needed.
//
// ## Pacing curves baked in
//
// The shader uses several internal easings (easeInSine,
// easeOutSine, easeOutCubic, plus a `pow(progress, 5)` shape on
// the gradient exponent) that are signature to the Aura-Glow
// pacing — they're not animation-profile choices. Users wanting
// BMW-exact behaviour should pair this with a Linear profile.

#include <noise.glsl>

// 2D simplex noise — MIT-licensed (Inigo Quilez). Definitions hosted
// in shared/noise.glsl so the seven shaders that need it pull from one
// source-of-truth rather than carrying a verbatim copy each.

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float easeInSine(float t)  { return 1.0 - cos(t * 1.5707963267948966); }
float easeOutSine(float t) { return sin(t * 1.5707963267948966); }
float easeOutCubic(float t) {
    float f = t - 1.0;
    return f * f * f + 1.0;
}

// HSV-rotate `color` by `hueOffset` (in [0,1]). Implemented in HSV
// space because the colour cycle is generated as RGB cosines and we
// want to slide the whole rainbow palette along a single hue
// parameter without changing saturation / value.
vec3 offsetHue(vec3 color, float hueOffset)
{
    float maxC  = max(max(color.r, color.g), color.b);
    float minC  = min(min(color.r, color.g), color.b);
    float delta = maxC - minC;
    float hue   = 0.0;
    if (delta > 0.0) {
        // Pick the dominant channel by magnitude rather than by
        // float-equality with `maxC` — for cosine-driven inputs where
        // two channels can land at the same value, exact `==` snaps to
        // an arbitrary branch and produces a hue jump.
        if (color.r >= color.g && color.r >= color.b)      hue = mod((color.g - color.b) / delta, 6.0);
        else if (color.g >= color.r && color.g >= color.b) hue = (color.b - color.r) / delta + 2.0;
        else                                               hue = (color.r - color.g) / delta + 4.0;
    }
    hue /= 6.0;

    float sat = (maxC > 0.0) ? (delta / maxC) : 0.0;
    float val = maxC;
    hue       = mod(hue + hueOffset, 1.0);

    float c = val * sat;
    float x = c * (1.0 - abs(mod(hue * 6.0, 2.0) - 1.0));
    float m = val - c;

    vec3 rgb;
    if      (hue < 1.0/6.0) rgb = vec3(c, x, 0.0);
    else if (hue < 2.0/6.0) rgb = vec3(x, c, 0.0);
    else if (hue < 3.0/6.0) rgb = vec3(0.0, c, x);
    else if (hue < 4.0/6.0) rgb = vec3(0.0, x, c);
    else if (hue < 5.0/6.0) rgb = vec3(x, 0.0, c);
    else                    rgb = vec3(c, 0.0, x);
    return rgb + m;
}

// Straight-alpha "over" composite, for pre-multiplied input/output
// where this layer (`over`) wants to be added on top of `under`
// without losing colour saturation. Denominator clamp guards the
// near-empty case `under.a ≈ 0 && over.a ≈ 1e-7` where the early
// `both-zero` check passes but the divide still produces huge RGB.
vec4 alphaOver(vec4 under, vec4 over)
{
    if (under.a == 0.0 && over.a == 0.0) return vec4(0.0);
    float a = mix(under.a, 1.0, over.a);
    return vec4(mix(under.rgb * under.a, over.rgb, over.a) / max(a, 1e-4), a);
}

// 16-tap radial blur of the window content. `radius` in pixels;
// `samples` is the inner-loop count (3 keeps the cost reasonable).
// Pre-multiplied texture read is summed in pre-multiplied space and
// divided by sample count — the result stays pre-multiplied.
vec4 blurredInputColor(vec2 uv, float radius, float samples)
{
    // Skip the 45-tap loop when the radius collapses — at progress=1
    // (fully visible state) the caller passes radius=0, and every tap
    // would resolve to the same coordinate. Falling through to a single
    // texture read saves 44 redundant samples per fragment on every
    // visible-state frame.
    if (radius < 0.5) {
        // boundaryMask (see noise.glsl) crops the sample to transparent
        // outside [0, 1] — at the "gone" state windowUV is scaled by 1.1
        // around the centre so the corners drift ~5% past the anchor,
        // and uTexture0's clamp-to-edge sampler would otherwise smear
        // the edge texel into a halo around the shrinking-orb silhouette.
        return surfaceColor(uv) * boundaryMask(uv);
    }
    vec4 acc = vec4(0.0);
    float totalWeight = 0.0;
    const float tau = 6.28318530718;
    const float dirs = 15.0;
    // int loop bound so iteration count exactly matches the divisor —
    // the float `s += 1.0 / samples` form can step `samples` or
    // `samples+1` times depending on float precision (s=0.999...
    // vs 1.000...), and the per-iteration `s` would no longer be
    // in bijection with the divisor. Floored to >=1 so a hostile
    // or buggy host that pushes samples to 0 or negative produces
    // one sample, not a divide-by-zero.
    int sampleCount = max(int(samples), 1);
    float sampleCountF = float(sampleCount);
    // Hoist iResolution floor out of the 45-tap inner loop —
    // invariant across all samples.
    vec2 flooredResolution = max(iResolution, vec2(1.0));
    for (float d = 0.0; d < tau; d += tau / dirs) {
        for (int i = 0; i < sampleCount; ++i) {
            float s = float(i) / sampleCountF;
            vec2 off = vec2(cos(d), sin(d)) * radius * (1.0 - s) / flooredResolution;
            vec2 tap = uv + off;
            float w = boundaryMask(tap);
            acc += surfaceColor(tap) * w;
            totalWeight += w;
        }
    }
    return acc / max(totalWeight, 1.0);
}

vec4 pTransition(vec2 uv, float t)
{
    // Aura-glow uses progress=visibility natively, matching iTime.
    float progress = clamp(t, 0.0, 1.0);

    // Aspect-correct UV blend. At progress=1 (visible state) we use
    // raw UV; at progress=0 (destroyed state) we use a 1:1
    // aspect-corrected UV so the gradient mask ends up circular even
    // on non-square windows.
    vec2 oneToOneUV = uv - 0.5;
    // Defensive floor against first-frame `iResolution = (0, 0)` —
    // matches the `blurredInputColor` divide above and the rest of
    // the suite's pattern (matrix/hexagon/honeycomb/pixelate). The
    // else branch would otherwise compute `0.0 / 0.0 = NaN` and
    // poison the entire composite.
    vec2 res = max(iResolution, vec2(1.0));
    if (res.x > res.y) {
        oneToOneUV.y *= res.y / res.x;
    } else {
        oneToOneUV.x *= res.x / res.y;
    }
    oneToOneUV += 0.5;
    uv = mix(oneToOneUV, uv, progress);

    // Super-ellipse gradient: `|x|^p + |y|^p`. p=2 is a circle, p=∞
    // is a square. With `pow(progress, 5)` the gradient shape stays
    // square (p=100) for most of the visible state and morphs to a
    // circle (p=2) at the gone state. The crop-mask threshold then
    // moves through the gradient from a square outline at visible
    // to a near-zero contour at gone, producing the "window crops
    // to a glowing orb that fades" reading.
    float shape    = mix(2.0, 100.0, pow(progress, 5.0));
    float gradient = pow(abs(uv.x - 0.5) * 2.0, shape) +
                     pow(abs(uv.y - 0.5) * 2.0, shape);
    gradient += simplex2D(vTexCoord + vec2(p_seedX, p_seedY)) * 0.5;

    // Glow rim mask. `(progress - gradient)/(edge+0.1)` is positive
    // where progress > gradient; after `1 - clamp(..., 0, 1)` the
    // mask is 1 OUTSIDE the contour and ramps to 0 INSIDE it. The
    // endpoint easing kills the glow at the visible state (where
    // there's nothing to dissolve from) and ramps it in toward the
    // gone state.
    float glowMask = (progress - gradient) / (p_edgeSize + 0.1);
    glowMask       = 1.0 - clamp(glowMask, 0.0, 1.0);
    glowMask      *= easeOutSine(min(1.0, (1.0 - progress) * 4.0));

    // Window UV: at visible state (progress=1) the scale is 1.0;
    // at gone state the window has scaled up 10%. easeOutCubic
    // makes the scale-up gentle.
    vec2 windowUV  = (vTexCoord - 0.5) * mix(1.1, 1.0, easeOutCubic(progress)) + 0.5;
    // Non-blur path mirrors the blurredInputColor mask above so off-
    // anchor windowUV samples (the 1.1× scale at the gone state pushes
    // corners ~5% past the anchor) clip cleanly to transparent instead
    // of smearing the edge texel via clamp-to-edge.
    vec4 windowCol = (p_blurAmount > 0.0)
        ? blurredInputColor(windowUV, (1.0 - progress) * p_blurAmount, 3.0)
        : surfaceColor(windowUV) * boundaryMask(windowUV);

    // Don't draw glow where the window itself is transparent
    // (window-content shaped, not square mask).
    glowMask *= windowCol.a;

    // Hue-cycling glow colour. cos(uv.xyx + offset) gives RGB
    // dancing around 0 with phase offsets (0, 2, 4) producing a
    // rainbow cycle. `progress * p_glowSpeed` ramps the cycle as
    // visibility grows — show accelerates, hide decelerates.
    vec3 glowColor = cos(progress * p_glowSpeed + uv.xyx + vec3(0.0, 2.0, 4.0));
    bool useRandom = p_randomColorFlag > 0.5;
    float hueShift = useRandom ? hash12(vec2(p_seedX, p_seedY)) : p_startHue;
    glowColor = offsetHue(glowColor, hueShift + 0.1);
    glowColor = clamp(glowColor * p_saturation, vec3(0.0), vec3(1.0));

    // Add glow additively (light emission), then alphaOver for a
    // non-additive component that helps on light themes.
    windowCol.rgb += glowColor * glowMask;
    windowCol = alphaOver(windowCol, vec4(glowColor, glowMask * 0.2));

    // Crop mask: blend between a soft-edged smoothstep and a hard
    // smoothstep based on `p_edgeHardness`. The window content is
    // visible where `gradient < progress` — the crop contour sits
    // at gradient=progress, expanding outward as progress grows.
    float softCrop = 1.0 - smoothstep(progress - 0.5, progress + 0.5, gradient);
    float hardCrop = 1.0 - smoothstep(progress - 0.05, progress + 0.05, gradient);
    float cropMask = mix(softCrop, hardCrop, p_edgeHardness);

    // Endpoint easings. The first multiplies the crop down toward
    // the gone state (progress→0) so the crop contour vanishes
    // smoothly. The second floors the crop to fully visible at the
    // visible state (progress→1) so we never dim the solid window.
    cropMask *= easeInSine(min(1.0, progress * 2.0));
    cropMask = max(cropMask, 1.0 - easeOutSine(min(1.0, (1.0 - progress) * 4.0)));

    windowCol.a *= cropMask;

    // Premultiply for the daemon's premultiplied-alpha blend pipeline
    // (src=One, dst=OneMinusSrcAlpha). The composite chain above mixes
    // premultiplied `texture(uTexture0,…)` with straight-alpha glow
    // additions and the alphaOver helper returns straight-alpha; if we
    // emitted that directly, fragments with low cropMask but nonzero
    // glow contribution would emit non-premultiplied RGB whose blend
    // result is `src.rgb + dst.rgb * (1 - src.a)` — additive over
    // whatever sits underneath. That is invisible in isolation
    // (transparent backdrop) but lights up snap-assist's tinted
    // content underneath as a residual halo. Premultiplying RGB by
    // the final alpha keeps the blend correct against any backdrop.
    return vec4(windowCol.rgb * windowCol.a, windowCol.a);
}
