// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Echo pointer shader — the real cursor sprite, stamped along the path it
// just took and tinted down the spectrum as each copy ages. The one pack in
// the family whose brush is the pointer itself rather than a drawn shape.
//
// THE IDEA. The brush is the cursor itself. Nothing else in the family binds
// uCursorSprite, so nothing else can say "that shape, the one you are
// actually pointing with, is what the trail is made of". Each echo is the
// real sprite stamped at a place the pointer has been, tinted further along
// the brand spectrum the older it is: cyan just behind the tip, rose at the
// end of the run.
//
// ECHO PLACEMENT is by SAMPLE INDEX across the live run, which IS a fixed
// time step: the host spaces its slots evenly in time over the trail window
// and appends one even when the pointer has not moved. So the echoes bunch up
// wherever the hand slowed, and spread out where it swept. Even spacing along
// the PATH would need a prefix sum of the arc length per fragment, which is
// not worth what it buys on a stamp this small. The newest sample is
// skipped: the compositor draws the real cursor there and a second copy
// under it just thickens the pointer.
//
// NO SPRITE. uPointerFlags.x is 0 wherever the sprite is not bound, which
// includes the settings preview (its stand-in arrow is a QML item, not a
// texture). The contract says a pack must read uCursorRect and skip the
// sample there, so the fallback below draws a convex arrow glyph fitted to
// that rect. That path is the ONLY thing the settings preview ever shows, so
// a glyph that silently covers nothing takes the whole pack down on the one
// stage where users pick it. It is a stand-in, not a cursor theme: it exists so the pack is
// legible in the preview, and the compositor always has the real sprite.
//
// REACH. Every echo's footprint is its sprite rect plus its glow, both held
// inside the reach of the SAMPLE it is stamped on, which is what the host
// inflated the damage rect around. The rect is clamped to the reach for the
// case of an unusually large cursor theme, so a big sprite shrinks rather
// than being cut off square at the rect's edge.
//
// COST. One texture fetch and one reject box per echo per fragment, over the
// trail's damage rect. Capped at kMaxEchoes; do not grow it.

const int kMaxEchoes = 10;
// How long the click flare lasts, matching the sibling packs' own windows.
const float kFlareSeconds = 0.3;

// Coverage of the fallback arrow glyph in local 0..1 sprite space, y down.
// A convex triangle: tip at the hotspot, opening down and right, which is
// where a left-pointing arrow's mass is. `soft` is the antialias feather in
// local units.
float arrowGlyph(vec2 local, float soft) {
    const vec2 tip = vec2(0.04, 0.02);
    const vec2 wing = vec2(0.10, 0.94);
    const vec2 barb = vec2(0.74, 0.62);
    // Signed half-plane distance to each edge. This winding puts the inside
    // on the NEGATIVE side of all three (check it at any opposite vertex:
    // d0 at `barb` is -0.659), so the interior is where the LARGEST of them
    // is still negative. Taking min() here instead would be negative over the
    // whole plane -- its maximum, at the centroid, is about -0.22 -- and the
    // glyph would never draw a single fragment.
    vec2 e0 = wing - tip;
    vec2 e1 = barb - wing;
    vec2 e2 = tip - barb;
    float d0 = (e0.x * (local.y - tip.y) - e0.y * (local.x - tip.x)) / max(length(e0), 1e-4);
    float d1 = (e1.x * (local.y - wing.y) - e1.y * (local.x - wing.x)) / max(length(e1), 1e-4);
    float d2 = (e2.x * (local.y - barb.y) - e2.y * (local.x - barb.x)) / max(length(e2), 1e-4);
    float inside = -max(max(d0, d1), d2);
    return smoothstep(-soft, soft, inside);
}

// The metadata trailSeconds. `lifetime` is this pack's trailWindowParam, so the
// host spaces the ring over it and inflates the damage rect for it; a value
// past this would walk samples the host has already stopped repainting.
const float kTrailSeconds = 2.0;

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    if (count < 2) {
        return vec4(0.0);
    }

    // One gate for the whole run, from the filtered speed: the raw per-sample
    // figure is one event pair and reads 0 whenever two events share a
    // millisecond, which gated per echo would blink them individually.
    float gate = pointerActivationGate(p_activationSpeed);
    if (gate <= 0.0) {
        return vec4(0.0);
    }

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    float reach = pointerReach();
    float lifetime = clamp(p_lifetime, 0.05, kTrailSeconds);

    // The sprite footprint, in device px, and where it sits relative to the
    // pointer. uCursorRect has the hotspot already applied, so the offset
    // from the newest sample is the offset for every echo. A host that does
    // not know the rect reports zeros; a plain square around the sample is
    // the honest stand-in for that, since there is no shape information to
    // place.
    vec2 spriteSize = uCursorRect.zw;
    vec2 spriteOffset = vec2(0.0);
    if (spriteSize.x > 1.0 && spriteSize.y > 1.0) {
        // Two clocks: uCursorRect is built at PAINT time from the live
        // cursor, while trail[0] is the newest ACCEPTED motion event. They
        // agree to within the sub-pixel movement the sampler suppressed,
        // because the head slot is refreshed in place on every accepted
        // event rather than only on an append. There is no live-position
        // uniform to use instead, so do not reach for one.
        spriteOffset = uCursorRect.xy - pointerTrailAt(0).xy;
    } else {
        spriteSize = vec2(24.0 * scale);
        spriteOffset = vec2(-2.0 * scale);
    }
    // The footprint has to fit the reach around the sample it is stamped on:
    // the whole sprite rect measured from the sample, plus the glow. Shrink
    // the sprite rather than clip it, so an oversized cursor theme on a small
    // reach reads as a smaller ghost instead of a square-cut one.
    //
    // The glow is capped against the reach FIRST. Sizing it off the unshrunk
    // sprite and then spending it on the budget leaves the one case the
    // shrink exists for -- a large cursor theme -- with a shrunken sprite and
    // a full-size glow, which then overruns the damage rect and gets the
    // square cut this block is here to avoid.
    float glowRadius = min(max(p_glow, 0.0) * 0.55 * max(spriteSize.x, spriteSize.y), reach * 0.6);
    // The furthest CORNER of the rect from the sample, not the furthest edge
    // on either axis: a per-axis max understates the corner by up to sqrt(2)
    // and lets it sit outside the reach the shrink just fitted.
    float span = length(vec2(max(abs(spriteOffset.x), abs(spriteOffset.x + spriteSize.x)),
                             max(abs(spriteOffset.y), abs(spriteOffset.y + spriteSize.y))));
    float budget = max(reach - glowRadius, reach * 0.35);
    if (span > budget) {
        float k = budget / span;
        spriteSize *= k;
        spriteOffset *= k;
    }

    bool hasSprite = uPointerFlags.x > 0.5;
    // The feather for the fallback glyph, one device px expressed in the
    // local 0..1 space the glyph is drawn in.
    float soft = 1.0 / max(min(spriteSize.x, spriteSize.y), 2.0);

    float flare = 0.0;
    float sincePress = pointerSincePress();
    if (p_clickFlare > 0.0 && uPointerPress.w > 0.5 && sincePress < kFlareSeconds) {
        float ct = sincePress / kFlareSeconds;
        flare = (1.0 - ct) * (1.0 - ct) * p_clickFlare;
    }

    // Only the LIVE run is walked: the samples behind it are outside the
    // damage rect, and the smoothing kernel clamps into this window.
    int live = pointerLiveCount(count, lifetime);
    if (live < 2) {
        return vec4(0.0);
    }
    // Never ask for more echoes than there are distinct samples to put them
    // on. The ring's spacing is set by the LONGEST trail window in the whole
    // chain, so a short-lifetime echo beside a long-lifetime pack can be down
    // to two or three live samples, and every echo would clamp onto the same
    // index -- N texture fetches stacked into one opaque stamp.
    int echoes = clamp(int(p_echoes + 0.5), 2, min(kMaxEchoes, max(live - 1, 2)));

    vec3 rgb = vec3(0.0);
    float alpha = 0.0;

    for (int j = 0; j < kMaxEchoes; ++j) {
        if (j >= echoes) {
            break;
        }
        // Evenly across the live run, starting one step in so the newest
        // sample (where the real cursor is drawn) is never doubled.
        float f = float(j + 1) / float(echoes);
        int idx = clamp(int(f * float(live - 1) + 0.5), 1, live - 1);
        vec2 at = pointerSmoothedAt(idx, live, p_smoothing);
        vec2 origin = at + spriteOffset;

        // Reject box: the sprite rect inflated by the glow.
        vec2 lo = origin - vec2(glowRadius);
        vec2 hi = origin + spriteSize + vec2(glowRadius);
        if (any(lessThan(px, lo)) || any(greaterThan(px, hi))) {
            continue;
        }

        // Age drives both the fade and the colour, so the two cannot disagree
        // about which echo is oldest. Taken from the SAMPLE's own age rather
        // than from j, so echoes bunched on a slow stretch of path are tinted
        // by when the pointer was there, not by their position in the list.
        float age = clamp(pointerTrailAt(idx).z / lifetime, 0.0, 1.0);
        float u = 1.0 - age;
        float fade = u * u * clamp(p_opacity, 0.0, 1.0);
        if (fade <= 0.0) {
            continue;
        }

        vec2 local = (px - origin) / max(spriteSize, vec2(1.0));
        float shape = 0.0;
        vec3 base = vec3(1.0);
        if (all(greaterThanEqual(local, vec2(0.0))) && all(lessThanEqual(local, vec2(1.0)))) {
            if (hasSprite) {
                // Implicit-LOD fetch inside divergent flow is undefined in
            // general; it is well defined here only because the sprite is
            // uploaded with no mipmaps and GL_LINEAR filtering, so no
            // derivative-driven level is ever selected. Adding mipmaps to the
            // upload would make this a real bug.
            vec4 texel = texture(uCursorSprite, local);
                // The sprite arrives premultiplied; the straight colour is
                // what the tint mixes against, and a fully transparent texel
                // has no colour to recover.
                shape = texel.a;
                base = texel.a > 0.001 ? texel.rgb / texel.a : vec3(1.0);
            } else {
                shape = arrowGlyph(local, soft);
            }
        }

        // A soft light around the echo, measured from the middle of the
        // sprite rect. It is what stops a slow drag reading as a row of flat
        // stickers. It is deliberately not bounded by `shape`: it paints
        // outside the sprite, which is the whole point of it.
        float glow = 0.0;
        if (glowRadius > 0.0) {
            vec2 centre = origin + spriteSize * 0.5;
            float d = length(px - centre) - 0.35 * min(spriteSize.x, spriteSize.y);
            glow = exp(-(max(d, 0.0) * max(d, 0.0)) / (2.0 * (glowRadius * 0.45) * (glowRadius * 0.45))) * 0.35;
        }

        vec3 tint = phosphorGradient(age);
        vec3 colour = mix(base, tint, clamp(p_tint, 0.0, 1.0));
        colour = mix(colour, vec3(1.0), clamp(0.5 * flare, 0.0, 1.0));

        // The shape and its glow are one coverage, so an echo cannot be
        // brighter than itself where the two overlap.
        float cover = clamp(max(shape, glow * shape + glow * 0.6), 0.0, 1.0) * fade * (1.0 + flare);
        if (cover <= 0.0) {
            continue;
        }
        float ca = clamp(cover, 0.0, 1.0);
        rgb += colour * ca;
        alpha += ca;
    }

    return premulAccumulated(rgb * gate, alpha * gate);
}
