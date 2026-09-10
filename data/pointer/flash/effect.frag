// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Flash pointer shader — a click-led pack. A press lights a soft round bloom
// at the press point with `spikeCount` thin spikes radiating from it, the
// shape a lens flare makes, and the whole thing is gone again in `duration`
// (a fifth of a second by default).
//
// MOTION DRAWS NOTHING. The body never reads uPointerTrail or uPointerVelocity,
// so a pointer that is merely moving produces transparent black everywhere.
//
// NOT A RING. The bloom is brightest at the middle at every moment of its
// life and its falloff is monotonic outward, so the silhouette never opens up
// into an annulus. It swells from 82 percent of its size to full over its
// life, a swell in place rather than an outward travel. Click Ripple owns the
// expanding ring.
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

// The spike count cap; the metadata maximum is this number.
const int kMaxSpikes = 12;

vec4 flashColour(float button) {
    return pointerButtonColour(button, p_colorLeft, p_colorRight, p_colorMiddle);
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
    // The reach the host resolved from `spikeLength`, in device px, read from
    // the uniform so the damage rect and the shader cannot drift.
    float limit = pointerReach();

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
    int spikes = clamp(int(p_spikeCount + 0.5), 0, kMaxSpikes);
    if (spikes > 0) {
        float len = limit * grow;
        // `across` is the distance from the spike's axis, so the parameter
        // (a thickness, as its description says) is halved to the half-width
        // the test below uses. Held under the reach so a wide spike on a
        // tiny reach still tapers rather than having its base cut square by
        // the box above.
        float width = min(0.5 * max(p_spikeWidth, 0.5) * scale, limit * 0.4);
        float base = radians(p_spikeAngle);
        for (int i = 0; i < kMaxSpikes; ++i) {
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
            // The tip floor is a device-px hairline, scaled like the width it
            // floors so the tip is the same share of the spike on any display.
            float w = max(width * taper, 0.35 * scale);
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
