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
//   • The orbit radius follows an averaged speed over the trail window rather
//     than the instantaneous one, so it swells and draws back in smoothly
//     instead of snapping.
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
const int kMaxTrail = 32;
const float kFadeStart = 1.0;
const float kQuietSeconds = 1.8;
const float kClickLife = 0.55;
// Speed (device px/s) at which the orbit is fully grown. Deliberately low:
// the settings preview's simulated pointer peaks near 324 px/s.
const float kFullSpeed = 220.0;

// Pointer position `lag` seconds ago, interpolated between the trail samples
// on either side of that age. Falls back to the newest sample when the trail
// does not reach that far back yet.
vec2 orbitLaggedCentre(int count, float lag) {
    vec2 newest = pointerTrailAt(0).xy;
    if (lag <= 0.0 || count < 2) {
        return newest;
    }
    vec2 prev = newest;
    float prevAge = pointerTrailAt(0).z;
    for (int i = 1; i < kMaxTrail; ++i) {
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

// Mean sample speed over the trail window, which is the eased stand-in for a
// smoothed speed the contract gives no state to keep.
float orbitMeanSpeed(int count) {
    float sum = 0.0;
    float n = 0.0;
    for (int i = 0; i < kMaxTrail; ++i) {
        if (i >= count) {
            break;
        }
        sum += pointerTrailAt(i).w;
        n += 1.0;
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
    float dotSize = max(p_dotSize, 0.25);
    float radiusMax = max(p_radius, 2.0);

    // Everything below is sized in LOGICAL px and converted once, so the reach
    // clamp can be stated in the same units as the `radius` parameter.
    float speedNorm = clamp(orbitMeanSpeed(count) / kFullSpeed, 0.0, 1.0);
    float grow = clamp(p_speedGrowth, 0.0, 1.0);
    float baseFrac = mix(1.0, 0.35, grow);
    float radius = radiusMax * (baseFrac + (1.0 - baseFrac) * speedNorm);

    // Click: a damped spring on the radius, at rest at both ends of its life.
    float sincePress = pointerSincePress();
    float push = max(p_clickPush, 0.0);
    if (push > 0.0 && uPointerPress.w > 0.5 && sincePress < kClickLife) {
        float k = sincePress / kClickLife;
        radius += push * exp(-4.5 * k) * sin(k * 14.0) * (1.0 - k);
    }
    // Reserve room for the dot inside the damage rect the host derives from
    // `radius`, without letting the reservation eat the ring.
    //
    // The reservation alone is not safe at the extremes: Dot size runs to 10
    // and Orbit radius starts at 8, so `radiusMax - 3*dotSize` goes negative
    // and every dot stacks on the centre — a legal pair of settings that turns
    // the pack into a single blob. Floor it at a fraction of the requested
    // radius so a large dot narrows the ring instead of deleting it.
    //
    // The floor does not fully close the overrun, and neither did the bare
    // reservation: when a dot's own drawn extent is a large share of the whole
    // reach there is no radius that fits both, because `reach` here tracks the
    // orbit radius alone and the shader has no uniform for the budget it is
    // spending. Bounded and much reduced, not eliminated.
    // Hoisted above the clamp because the reservation below has to know it: a
    // smeared dot is elongated along the travel axis by this much, so
    // reserving a fixed three dot radii would leave the smeared end hanging
    // outside the rect no matter what the cutoff does.
    float smear = clamp(p_smear, 0.0, 1.0) * speedNorm;
    float stretch = 1.0 + smear * 2.0;

    radius = clamp(radius, 0.0, max(radiusMax - 3.0 * dotSize * stretch, radiusMax * 0.4)) * scale;

    vec2 centre = orbitLaggedCentre(count, max(p_lag, 0.0));
    vec2 drift = uPointerVelocity.xy;
    float dotPx = dotSize * scale;
    float spin = iTime * p_orbitRate * TAU;

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
        vec2 tangent = vec2(-radial.y, radial.x) * (radius * p_orbitRate * TAU);
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
        // Compact support, like the shapes in Halo and Flash. Both gaussians
        // are still visible where the reservation above runs out, so the dot
        // met the damage rect's edge at a level you could see and was cut off
        // square.
        //
        // The window goes on the SUM, in the same stretched space the shape is
        // drawn in. Windowing only the halo left `body` — the very term the
        // smear elongates — unbounded, and windowing in unstretched space
        // would clip the smear back toward a circle, which is the shape's whole
        // point. Three dot radii in stretched space is what the reservation
        // now reserves, so the two agree in every direction.
        float cover = clamp(body + halo, 0.0, 1.0) * (1.0 - smoothstep(2.0, 3.0, sqrt(q))) * live;
        if (cover <= 0.0) {
            continue;
        }
        float t = dots > 1 ? float(j) / float(dots - 1) : 0.0;
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
