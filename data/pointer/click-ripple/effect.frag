// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Click Ripple pointer shader — an expanding ring from the last press point
// and a thinner one from the last release, each easing out and fading over
// `duration`. Coloured per button. Layer "above": the host hides the cursor
// and paints the sprite after this, so the rings read on top of it. Both
// rings are gone by `duration`, which stays within trailSeconds.

const float kTrailSeconds = 0.9;

vec4 buttonColour(float button) {
    if (button > 2.5) {
        return p_colorMiddle;
    }
    if (button > 1.5) {
        return p_colorRight;
    }
    return p_colorLeft;
}

// Ring coverage for an event at `origin`, `since` seconds old, at the
// given line thickness (device px). Radius eases out (cubic) to `maxRadius`.
float ring(vec2 px, vec2 origin, float since, float duration, float maxRadius, float thickness) {
    float t = since / duration;
    if (t >= 1.0) {
        return 0.0;
    }
    float ease = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
    float radius = maxRadius * ease;
    float d = abs(length(px - origin) - radius);
    float half_ = 0.5 * thickness;
    float band = 1.0 - smoothstep(half_ - 0.75, half_ + 0.75, d);
    float fade = (1.0 - t) * (1.0 - t);
    return band * fade;
}

vec4 pPointer(vec2 uv) {
    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float duration = clamp(p_duration, 0.05, kTrailSeconds);
    float thickness = max(p_thickness, 0.5) * scale;
    // The ring is drawn as a BAND around `radius`, so the painted edge is half
    // a thickness plus the antialias feather beyond it. The metadata's reach
    // covers the radius alone, which is exact at Size's maximum and leaves the
    // band hanging outside the damage rect the host buys — a flat-cut ring edge
    // at large sizes. Take the band out of the budget rather than out of the
    // user's Size. pointerReach() is that budget as the host actually resolved
    // it, so there is no constant here to drift from the metadata.
    float maxRadius = min(0.5 * max(p_size, 2.0) * scale, pointerReach() - 0.5 * thickness - 0.75);

    vec3 rgb = vec3(0.0);
    float alpha = 0.0;

    float sincePress = pointerSincePress();
    if (uPointerPress.w > 0.5 && sincePress < duration) {
        float a = ring(px, uPointerPress.xy, sincePress, duration, maxRadius, thickness);
        vec4 c = buttonColour(uPointerPress.w);
        a *= c.a;
        rgb += c.rgb * a;
        alpha += a;
    }

    float sinceRelease = pointerSinceRelease();
    if (uPointerRelease.w > 0.5 && sinceRelease < duration) {
        float a = ring(px, uPointerRelease.xy, sinceRelease, duration, maxRadius * 0.8, thickness * 0.5);
        vec4 c = buttonColour(uPointerRelease.w);
        a *= c.a * 0.8;
        rgb += c.rgb * a;
        alpha += a;
    }

    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    // The two rings are already premultiplied sums; clamp coverage and keep
    // the colour in proportion.
    float clamped = min(alpha, 1.0);
    return vec4(rgb * (clamped / alpha), clamped);
}
