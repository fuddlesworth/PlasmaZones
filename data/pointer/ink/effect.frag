// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Ink pointer shader — a calligraphic brush stroke. The one pack in this
// family that is not made of light: it returns an opaque matte colour with no
// bloom and no additive brightening, so it sits on the desktop like a mark on
// paper rather than glowing over it.
//
// Width answers to speed INVERSELY, which is what makes it read as a brush.
// Every other pack here gets wider the faster you move. A real brush or pen
// does the opposite: press slowly and it spreads, whip it across the page and
// it thins to a hairline. That inversion is the whole identity of the pack.
//
// An earlier revision broke the edge up with two octaves of value noise to
// suggest bristles. It read as grain and dirt rather than as a brush, so the
// noise is gone entirely. The edge is a clean antialiased band and the
// character comes from the width curve and the drying instead.
//
// Drying is age-asymmetric and deliberate: a stroke clears from its OLDEST end
// forward, so the tail lifts off the page first and the ink at the cursor is
// the last to go. Nothing else in the family fades that way round.
//
// `smoothing` goes through the shared pointerSmoothedAt(), like every other
// path pack, so two packs in one chain trace the same curve from the same
// pointer. It defaults to 0.5, the value the pack used to hardcode.

const int kMaxTrail = 32;

// Speed at which the stroke reaches its thinnest, in px/s.
const float kThinSpeed = 1200.0;

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float halfWidth = 0.5 * max(p_width, 1.0) * scale;
    float lifetime = max(p_lifetime, 0.05);
    float bleed = clamp(p_bleed, 0.0, 1.0);

    float cover = 0.0;

    // ── The stroke ──
    if (count >= 2) {
        float cull = halfWidth * 1.5 + 2.0 * scale;
        for (int i = 0; i < kMaxTrail - 1; ++i) {
            if (i + 1 >= count) {
                break;
            }
            vec4 a = pointerTrailAt(i);
            vec4 b = pointerTrailAt(i + 1);
            if (a.z >= lifetime) {
                break;
            }

            vec2 pa = pointerSmoothedAt(i, count, p_smoothing);
            vec2 pb = pointerSmoothedAt(i + 1, count, p_smoothing);
            vec2 ab = pb - pa;
            float len = length(ab);
            if (len < 1e-4) {
                continue;
            }
            vec2 lo = min(pa, pb) - cull;
            vec2 hi = max(pa, pb) + cull;
            if (px.x < lo.x || px.y < lo.y || px.x > hi.x || px.y > hi.y) {
                continue;
            }

            float t = clamp(dot(px - pa, ab) / (len * len), 0.0, 1.0);
            float d = length(px - (pa + ab * t));
            float age = mix(a.z, b.z, t);
            if (age >= lifetime) {
                continue;
            }
            float remain = 1.0 - age / lifetime;

            // The brush curve: broad where the hand was slow, thin where it
            // was quick.
            float speedNorm = clamp(mix(a.w, b.w, t) / kThinSpeed, 0.0, 1.0);
            float w = halfWidth * mix(1.0, 0.30, sqrt(speedNorm));

            // Wet ink spreads a little into the paper for the first part of
            // its life, then stops. It never spreads again once dry.
            float wet = 1.0 - smoothstep(0.0, 0.35, 1.0 - remain);
            w *= 1.0 + 0.18 * bleed * wet;
            w = max(w, 0.4 * scale);

            // A clean antialiased edge. This is the only softness in the pack.
            float band = 1.0 - smoothstep(w - 0.75, w + 0.75, d);

            // Drying: the oldest end of the stroke lifts first. Held at full
            // opacity for most of the life, then taken off quickly, which is
            // what makes the tail vanish rather than dim evenly.
            float dry = smoothstep(0.0, 0.30, remain);
            cover = max(cover, band * dry);
        }
    }

    // ── The blot ──
    // A click lands a drop of ink. Round and clean, on the same wet then dry
    // curve as the stroke. Bounded well inside the reach `width` buys.
    float blot = clamp(p_blot, 0.0, 0.7);
    float since = pointerSincePress();
    if (blot > 0.0 && uPointerPress.w > 0.5 && since < lifetime) {
        float remain = 1.0 - since / lifetime;
        float wet = 1.0 - smoothstep(0.0, 0.35, 1.0 - remain);
        // Widest while WET and drawing back in as it dries, the same shape and
        // the same direction as the stroke's own width term. Gated on Bleed for
        // the same reason, so setting Bleed to zero really does keep one width
        // everywhere.
        float r = halfWidth * blot * 2.0 * (1.0 + 0.18 * bleed * wet);
        float d = length(px - uPointerPress.xy);
        float band = 1.0 - smoothstep(r - 0.75, r + 0.75, d);
        cover = max(cover, band * smoothstep(0.0, 0.30, remain));
    }

    float alpha = clamp(cover * p_color.a, 0.0, 1.0);
    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    return premul(p_color.rgb, alpha);
}
