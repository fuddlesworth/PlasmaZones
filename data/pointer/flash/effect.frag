// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Flash pointer shader — a click-led pack. A press lights a soft round bloom
// at the press point with `spikeCount` thin spikes crossing through it, the
// shape a lens flare makes, and the whole thing is gone again in `duration`
// (a fifth of a second by default).
//
// MOTION DRAWS NOTHING. The body never reads uPointerTrail or uPointerVelocity,
// so a pointer that is merely moving produces transparent black everywhere.
//
// NOT A RING. The bloom is brightest at the middle at every moment of its
// life and its falloff is monotonic outward, so the silhouette never opens up
// into an annulus. It grows by 18 percent over its life, which is a swell in
// place rather than an outward travel. Click Ripple owns the expanding ring.
//
// The fade is pow(1 - t, 4): almost all the light is spent in the first third
// of the life, so it snaps rather than lingering, and it is exactly zero at
// t = 1.
//
// Coloured per button, following the Click Ripple convention. Layer "below",
// like every pack here except Click Ripple: the flash reads fine under the
// cursor, and staying below keeps the pointer on the hardware plane.
//
// REACH. Both shapes use compact-support falloffs that are exactly zero at
// their outer edge, and both edges are clamped to `spikeLength` (the parameter
// `reachParam` names), so nothing is painted outside the damage rect the host
// derives from it.

const float kDegreesToRadians = 0.01745329252;

vec4 flashColour(float button) {
    if (button > 2.5) {
        return p_colorMiddle;
    }
    if (button > 1.5) {
        return p_colorRight;
    }
    return p_colorLeft;
}

vec4 pPointer(vec2 uv) {
    float intensity = max(p_intensity, 0.0);
    if (intensity <= 0.0 || uPointerPress.w < 0.5) {
        return vec4(0.0);
    }

    float duration = clamp(p_duration, 0.05, 0.5);
    float since = pointerSincePress();
    if (since >= duration) {
        return vec4(0.0);
    }

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float limit = max(p_spikeLength, 1.0) * scale;

    vec2 rel = px - uPointerPress.xy;
    if (abs(rel.x) > limit || abs(rel.y) > limit) {
        return vec4(0.0);
    }

    float t = since / duration;
    // Sharp snap: a quartic fade reaching exactly zero at the end of the life.
    float env = 1.0 - t;
    env = env * env * env * env;
    // A slight swell in place, never past the declared reach.
    float grow = 0.82 + 0.18 * t;

    float coverage = 0.0;

    // Round bloom, brightest at the middle, zero at its own edge.
    float bloom = min(max(p_bloomSize, 0.5) * scale * grow, limit);
    float d = length(rel);
    if (d < bloom) {
        float q = 1.0 - (d * d) / (bloom * bloom);
        coverage += q * q * q;
    }

    // Spikes: thin bright lines through the middle, longest along their own
    // axis and tapering to nothing at the tips.
    int spikes = clamp(int(p_spikeCount + 0.5), 0, 12);
    if (spikes > 0) {
        float len = limit * grow;
        float width = max(p_spikeWidth, 0.25) * scale;
        float base = p_spikeAngle * kDegreesToRadians;
        for (int i = 0; i < 12; ++i) {
            if (i >= spikes) {
                break;
            }
            float angle = base + float(i) / float(spikes) * TAU;
            vec2 dir = vec2(cos(angle), sin(angle));
            float along = dot(rel, dir);
            if (along <= 0.0 || along >= len) {
                continue;
            }
            float across = abs(rel.x * dir.y - rel.y * dir.x);
            float taper = 1.0 - along / len;
            float w = max(width * taper, 0.35);
            if (across >= w) {
                continue;
            }
            float q = 1.0 - (across * across) / (w * w);
            coverage += q * q * taper * taper;
        }
    }

    if (coverage <= 0.0) {
        return vec4(0.0);
    }

    vec4 c = flashColour(uPointerPress.w);
    float alpha = min(coverage * env * intensity * c.a, 1.0);
    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    return premul(c.rgb, alpha);
}
