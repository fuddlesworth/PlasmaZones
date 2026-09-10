// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Orbit pointer shader — a few small dots circling the pointer. Nothing is
// drawn along the path, which is the whole point of the pack: every other
// trail pack paints where the pointer has been, this one paints only around
// where it is.
//
// LAG IS THE CHARACTER. Two things lag, and both come out of the trail
// history rather than any stored state:
//
//   • The orbit centre is the pointer position `lag` seconds ago, read out of
//     the trail by interpolating between the two samples that straddle that
//     age. Standing still that is the cursor itself; moving fast the ring
//     hangs behind the cursor as if dragged.
//   • The orbit radius follows the speed averaged over the samples of the
//     last kSpeedWindowSeconds rather than the instantaneous one, so it
//     swells and draws back in smoothly instead of snapping.
//
// Each dot is a round gaussian point, optionally stretched along its own
// direction of travel (its orbital tangent plus the centre's motion) into a
// short smear at speed.
//
// A press makes the ring scatter outward and spring back: a damped
// oscillation added to the radius over kClickLife, ending exactly at rest.
//
// FADE: the dots keep orbiting while the pointer is parked, so the pack owns
// its own quiet. Everything is multiplied by an idle envelope that starts
// easing down at kFadeStart and is exactly zero at kQuietSeconds, which is
// inside the metadata trailSeconds. A parked pointer therefore gets a couple
// of seconds of orbiting, then nothing, and the host stops repainting.

const int kMaxDots = 6;
const float kFadeStart = 1.0;
const float kQuietSeconds = 1.8;
const float kClickLife = 0.55;
// Speed (device px/s) at which the orbit is fully grown. Deliberately low:
// the settings preview's simulated pointer peaks near 324 px/s.
const float kFullSpeed = 220.0;
// Age window the radius's averaged speed is read over. The ring is never
// purged while the pointer rests and a park appends only one speed-0 sample
// on the first move after it, so an average over EVERY slot would read the
// previous stroke's speeds for a whole window after a pause and pop the ring
// out to its old radius instead of growing it from rest.
const float kSpeedWindowSeconds = 0.6;

// The orbit centre: the pointer position `lag` seconds ago, interpolated
// between the trail samples on either side of that age. Falls back to the
// newest sample when the trail does not reach that far back yet. Stops at
// the first sample past the lag, which at the default lag is the second or
// third slot, so a fragment the reject box below throws out pays for almost
// none of the walk.
vec2 orbitCentre(int count, float lag) {
    vec4 newest = pointerTrailAt(0);
    if (lag <= 0.0 || count < 2) {
        return newest.xy;
    }
    vec2 prev = newest.xy;
    float prevAge = newest.z;
    for (int i = 1; i < kPointerTrailCapacity; ++i) {
        if (i >= count) {
            break;
        }
        vec4 s = pointerTrailAt(i);
        if (s.z >= lag) {
            float span = max(s.z - prevAge, 1e-4);
            return mix(prev, s.xy, clamp((lag - prevAge) / span, 0.0, 1.0));
        }
        prev = s.xy;
        prevAge = s.z;
    }
    return prev;
}

// Mean sample speed over the samples younger than kSpeedWindowSeconds, the
// eased stand-in for a smoothed speed the contract gives no state to keep.
// Ages are monotonic in the index, so the walk ends at the first old sample.
//
// Age-WEIGHTED, not a flat mean: on the compositor a resting pointer sends
// no events, so the in-window set empties from its oldest end until only the
// head is left, and a flat mean would hold the final stroke speed until the
// head crossed the window edge and then drop to zero in one frame, popping
// the ring inward while it is still fully live. With a weight that reaches
// zero at the edge each sample fades out of the average as it ages, so the
// draw-back is the ease the header promises on both runtimes.
float orbitMeanSpeed(int count) {
    float sum = 0.0;
    float n = 0.0;
    for (int i = 0; i < kPointerTrailCapacity; ++i) {
        if (i >= count) {
            break;
        }
        vec4 s = pointerTrailAt(i);
        if (s.z >= kSpeedWindowSeconds) {
            break;
        }
        float w = 1.0 - s.z / kSpeedWindowSeconds;
        sum += s.w * w;
        n += w;
    }
    return n > 0.0 ? sum / n : 0.0;
}

vec4 pPointer(vec2 uv) {
    int count = pointerTrailCount();
    if (count < 1) {
        return vec4(0.0);
    }

    // A click wakes the ring back up even under a pointer that has been
    // parked past the idle fade, so the scatter is never invisible.
    float live = clamp(1.0 - smoothstep(kFadeStart, kQuietSeconds, pointerIdleSeconds()), 0.0, 1.0);
    if (uPointerPress.w > 0.5) {
        live = max(live, clamp(1.0 - pointerSincePress() / kClickLife, 0.0, 1.0));
    }
    if (live <= 0.0) {
        return vec4(0.0);
    }

    vec2 px = pointerPixel(uv);
    float scale = pointerScale();
    int dots = clamp(int(p_dots + 0.5), 1, kMaxDots);
    // The budget everything below has to fit in: the reach the host resolved
    // from the `radius` parameter, in device px, read from the uniform so the
    // damage rect and the shader cannot disagree about it.
    float reachPx = pointerReach();

    vec2 centre = orbitCentre(count, max(p_lag, 0.0));
    // Nothing is painted further than the reach from the centre, so a
    // fragment outside that box is done before the speed walk and the six
    // dots' trigonometry. The damage rect spans the whole live trail (up to
    // two seconds of path), which for a fast sweep is most of the screen.
    if (any(greaterThan(abs(px - centre), vec2(reachPx)))) {
        return vec4(0.0);
    }

    float speedNorm = clamp(orbitMeanSpeed(count) / kFullSpeed, 0.0, 1.0);
    float grow = clamp(p_speedGrowth, 0.0, 1.0);
    float baseFrac = mix(1.0, 0.35, grow);
    float radius = reachPx * (baseFrac + (1.0 - baseFrac) * speedNorm);

    // Click: a damped spring on the radius, at rest at both ends of its life.
    float sincePress = pointerSincePress();
    float push = max(p_clickPush, 0.0) * scale;
    if (push > 0.0 && uPointerPress.w > 0.5 && sincePress < kClickLife) {
        float k = sincePress / kClickLife;
        radius += push * exp(-4.5 * k) * sin(k * 14.0) * (1.0 - k);
    }

    // A dot's drawn extent is three dot radii (the compact window below),
    // elongated along its travel by `stretch` when smeared. That extent comes
    // out of the reach so the dot never meets the damage rect's edge: the
    // ring radius is capped at reach minus the extent, and when a legal pair
    // of settings (Dot size up to 10 against an Orbit radius from 8) leaves
    // no room for both, the DOT is shrunk to fit rather than the ring being
    // collapsed onto the centre or the extent being let hang outside the
    // rect. The dot may take at most 60 percent of the reach, so the ring
    // always keeps some radius to orbit on.
    float smear = clamp(p_smear, 0.0, 1.0) * speedNorm;
    float stretch = 1.0 + smear * 2.0;
    float dotPx = min(max(p_dotSize, 0.25) * scale, reachPx * 0.6 / (3.0 * stretch));
    radius = clamp(radius, 0.0, reachPx - 3.0 * dotPx * stretch);

    vec2 drift = uPointerVelocity.xy;
    // The spin rate is nudged onto a divisor of the iTime wrap so the ring
    // does not jump at the wrap; see pointerWrapSafeRate.
    float orbitRate = pointerWrapSafeRate(max(p_orbitRate, 0.0));
    float spin = iTime * orbitRate * TAU;

    vec3 rgb = vec3(0.0);
    float alpha = 0.0;
    for (int j = 0; j < kMaxDots; ++j) {
        if (j >= dots) {
            break;
        }
        float angle = spin + float(j) / float(dots) * TAU;
        vec2 radial = vec2(cos(angle), sin(angle));
        vec2 pos = centre + radial * radius;
        vec2 rel = px - pos;

        // Travel direction: the orbital tangent plus the centre's own motion.
        vec2 tangent = vec2(-radial.y, radial.x) * (radius * orbitRate * TAU);
        vec2 travel = tangent + drift;
        float travelLen = length(travel);
        float along = rel.x;
        float across = rel.y;
        float dotStretch = 1.0;
        if (travelLen > 1e-3 && smear > 0.0) {
            vec2 dir = travel / travelLen;
            along = dot(rel, dir);
            across = dot(rel, vec2(-dir.y, dir.x));
            dotStretch = stretch;
        }
        float da = along / (dotPx * dotStretch);
        float db = across / dotPx;
        float q = da * da + db * db;
        float body = exp(-q * 0.5);
        float halo = exp(-q / 8.0) * 0.35;
        // Compact support, like the shapes in Halo and Flash: exactly zero at
        // three dot radii, which is the extent the radius cap above reserves
        // inside the reach, so the two agree in every direction.
        //
        // The window goes on the SUM, in the same stretched space the shape is
        // drawn in. Windowing only the halo left `body` — the very term the
        // smear elongates — unbounded, and windowing in unstretched space
        // would clip the smear back toward a circle, which is the shape's whole
        // point.
        float cover = clamp(body + halo, 0.0, 1.0) * (1.0 - smoothstep(2.0, 3.0, sqrt(q))) * live;
        if (cover <= 0.0) {
            continue;
        }
        // Ping-pong, not a straight ramp: the dots sit on a closed circle, so
        // a ramp that ends on colorB puts the last dot right beside the first,
        // and the seam between the two colours lands there. Walking back down
        // the ramp keeps neighbours close in colour all the way round. The
        // half-turn phase puts dot 0 at colorA, which is what "First colour"
        // promises.
        float t = abs(fract(float(j) / float(dots) + 0.5) * 2.0 - 1.0);
        vec3 c = mix(p_colorA.rgb, p_colorB.rgb, t);
        float ca = cover * mix(p_colorA.a, p_colorB.a, t);
        rgb += c * ca;
        alpha += ca;
    }

    if (alpha <= 0.0) {
        return vec4(0.0);
    }
    float clamped = min(alpha, 1.0);
    return vec4(rgb * (clamped / alpha), clamped);
}
