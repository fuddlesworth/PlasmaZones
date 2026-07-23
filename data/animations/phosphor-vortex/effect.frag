// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Vortex — the Phosphor set's window-move shader. Grab a window
// and the vortex inhales it: the surface stretches into streamlines flowing
// radially into a plasma cloud around the cursor (brand accent gradient,
// cyan #22D3EE → blue #3B82F6 → purple #A855F7 → rose #F43F5E), eroding as
// it feeds in — the intact content never rotates, only the plasma arms
// spin. For as long as the drag is held — moving or not — the window is
// gone and the plasma orbits the pointer, trailing along the drag path
// while you move. Release, and the plasma exhales it back.
//
// DRIVE MODEL — dissolve = iTime, nothing else. The held-move runtime plays
// iTime 0→1 at the grab, PINS it at 1 for the whole hold (stationary or
// not), and ramps it 1→0 after release (the release leg stamped by
// windowFinishUserMovedResized, ramped in paint_pipeline.cpp). Deliberately
// NOT velocity-driven: pausing mid-drag used to rematerialise the window
// and re-dissolve it on the next twitch, which read as flicker. Velocity
// and the drag trail only shape the plasma (lead, stretch), never whether
// it exists.
//
// The vortex centres on the real cursor: iMouse.zw is the cursor in
// frame-normalised coordinates on the KWin animation path (card centre as
// the fallback when the sentinel reports the cursor outside the frame).
//
// SURFACE EXTENT — fboExtent "surface": the runtime lays an output-spanning
// quad, so the plasma can swirl and trail outside the frame. anchorRemap
// converts fragments into card space; displaced surface taps are faded by
// texelInsideMask so texture clamp-to-edge never smears.
//
// Output is premultiplied alpha: the imploding surface taps stay
// premultiplied, the plasma is emissive but alpha-carried and bounded.

#include <noise.glsl>
#include <anchor_remap.glsl>

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose.
vec3 fluxGradient(float t) {
    vec3 cyan   = length(p_colorCyan.rgb)   > 0.01 ? p_colorCyan.rgb   : vec3(0.133, 0.827, 0.933);
    vec3 blue   = length(p_colorBlue.rgb)   > 0.01 ? p_colorBlue.rgb   : vec3(0.231, 0.510, 0.965);
    vec3 purple = length(p_colorPurple.rgb) > 0.01 ? p_colorPurple.rgb : vec3(0.659, 0.333, 0.969);
    vec3 rose   = length(p_colorRose.rgb)   > 0.01 ? p_colorRose.rgb   : vec3(0.957, 0.247, 0.369);
    t = clamp(t, 0.0, 1.0) * 3.0;
    vec3 c = mix(cyan, blue, clamp(t, 0.0, 1.0));
    c = mix(c, purple, clamp(t - 1.0, 0.0, 1.0));
    c = mix(c, rose, clamp(t - 2.0, 0.0, 1.0));
    return c;
}

// Seamless ping-pong of an unbounded coordinate into [0, 1].
float pingPong(float x) {
    x = fract(x);
    return 1.0 - abs(2.0 * x - 1.0);
}

// Full turn; the shared headers define no TAU on the KWin path.
const float kTau = 6.28318530718;

// Fade a card-space sample to zero as it approaches the edge of the texture
// it actually resolves into (the padded surface layer when one is active,
// the expanded uTexture0 otherwise). Displaced taps past that extent would
// otherwise clamp-to-edge and smear the border pixels into the swirl.
float texelInsideMask(vec2 uv) {
    vec4 r = (iHasSurfaceLayer != 0) ? iLayerRectInTexture : iAnchorRectInTexture;
    vec2 tc = r.xy + uv * r.zw;
    vec2 e = smoothstep(vec2(0.0), vec2(0.004), tc)
           * (vec2(1.0) - smoothstep(vec2(0.996), vec2(1.0), tc));
    return e.x * e.y;
}

vec4 pTransition(vec2 uv, float t) {
    vec2 auv = anchorRemap(uv);

    // Dissolve IS the leg progress: 0→1 at grab, pinned 1 while held,
    // 1→0 on the release leg. See the drive-model note in the header.
    float dissolve = clamp(t, 0.0, 1.0);
    if (dissolve <= 0.004) {
        return surfaceColor(auv);
    }
    float dEase = dissolve * dissolve * (3.0 - 2.0 * dissolve);

    // ── Geometry in aspect-corrected card units, vortex centred on the
    // cursor (frame-normalised iMouse.zw; centre fallback on the outside
    // sentinel). ──
    vec2 anchor = max(iAnchorSize, vec2(1.0));
    float aspect = anchor.x / anchor.y;
    vec2 cur = (iMouse.x >= 0.0) ? iMouse.zw : vec2(0.5);
    vec2 cpc = (cur - 0.5) * vec2(aspect, 1.0);
    vec2 pc  = (auv - 0.5) * vec2(aspect, 1.0);
    vec2 rel = pc - cpc;
    float r = length(rel);
    float baseAng = atan(rel.y, rel.x);

    // Continuous rotation for as long as the effect is alive — iFrame is
    // monotonic through the whole held leg, so the plasma keeps swirling
    // however long the drag holds still. Frame-keyed spin is refresh-rate
    // dependent (a 144 Hz display swirls 2.4x faster than 60 Hz); accepted:
    // the swirl is decorative, has no timeline to keep, and iDate.w (the
    // rate-independent alternative) wraps at midnight.
    float spin = float(iFrame) * 0.03 * clamp(p_spin, 0.0, 3.0);
    float streaks = clamp(p_streaks, 0.0, 1.0);

    vec4 outC = vec4(0.0);

    // ── The window body: suction streamlines. NO rotation of the intact
    // content — the surface displaces radially toward the cursor, each tap
    // pulled a different depth along its ray so the smear reads as motion
    // blur INTO the vortex. Angle-striped gating (constant along each ray)
    // tears the smear into discrete streamlines feeding the cloud, and a
    // noise erosion front breaks the smear up as it nears the cursor. Note
    // the erosion term rides the per-tap WEIGHTS, and the accumulator is
    // renormalised by those same weights — so a uniform erosion across the
    // six taps cancels and only inter-tap variance survives (that variance
    // is the visible break-up). What actually fades the body is `holdBody`
    // below. The whole body fades out as the dissolve completes, so at full
    // hold the window is GONE. ──
    float holdBody = 1.0 - dEase;
    if (holdBody > 0.003) {
        float suction = clamp(p_suction, 0.0, 1.0);
        vec4 acc = vec4(0.0);
        float wsum = 0.0;
        const int kTaps = 6;
        for (int k = 0; k < kTaps; ++k) {
            float f = float(k) / float(kTaps - 1);

            // Radial pull: the drawn body contracts toward the cursor, so
            // the source sample lies further out along the same ray. Deeper
            // taps have travelled further in — that spread IS the stretch.
            float pull = mix(1.0, mix(0.6, 0.10, suction), dEase * (0.35 + 0.65 * f));
            vec2 suv = (cpc + rel / pull) / vec2(aspect, 1.0) + 0.5;

            vec4 s = surfaceColor(suv) * texelInsideMask(suv);

            // Erosion front: hash noise anchored to the SOURCE coordinates
            // (it rides the content, not the screen), biased so material
            // close to the cursor — about to join the cloud — goes first.
            float n = niriHash(floor(suv * vec2(40.0 * aspect, 40.0)));
            float front = dEase * (0.45 + 0.55 * f) + exp(-r * 2.5) * 0.35 * dEase;
            float erode = 1.0 - smoothstep(n - 0.18, n + 0.18, front);

            // Streamlines: stripes in angle around the cursor, constant
            // along each ray, so the pulled content separates into feeding
            // filaments instead of a uniform zoom blur.
            float stripe = sin(baseAng * 14.0 + niriHash(vec2(float(k), 3.7)) * kTau);
            float gateF = mix(1.0, smoothstep(-0.6, 0.9, stripe),
                              streaks * dEase * (0.3 + 0.7 * f));

            float w = exp(-1.6 * f) * gateF * erode;
            acc += s * w;
            wsum += w;
        }
        acc /= max(wsum, 1e-4);
        outC = acc * holdBody;
    }

    // ── The plasma cloud: procedural filament arms orbiting the cursor,
    // ghosted back along the real drag path (iMoveTrail — zero at rest, so
    // holding still gives a compact vortex and moving stretches it into a
    // comet). Forms as the window collapses, unwinds on the release leg. ──
    float cloudGain = smoothstep(0.1, 0.75, dEase) * clamp(p_glow, 0.0, 2.0);
    if (cloudGain > 0.003) {
        float cloudR = max(p_cloudSize, 20.0) / anchor.y;
        vec3 cloudC = vec3(0.0);
        float cloudA = 0.0;

        const int kGhosts = 6;
        for (int g = 0; g < kGhosts; ++g) {
            float gf = float(g) / float(kGhosts - 1);

            // Ghost centres ride the drag history (old origin - now).
            vec2 gOffPc = (iMoveTrail[int(min(gf * 12.0, 15.0))] / anchor) * vec2(aspect, 1.0);
            vec2 grel = pc - (cpc + gOffPc);
            float gr = length(grel) / cloudR;
            float gAng = atan(grel.y, grel.x);

            // Two counter-winding filament arms plus a hot core.
            float a1 = sin(gAng * 3.0 + gr * 4.0 - spin * (1.0 + 0.2 * gf) + gf * 7.0);
            float a2 = sin(gAng * 5.0 - gr * 6.5 + spin * 1.4 + gf * 3.1);
            float fil = pow(0.5 + 0.5 * a1, 3.0) * 0.75 + pow(0.5 + 0.5 * a2, 4.0) * 0.55;
            float fall = exp(-gr * gr * 1.4);
            float core = exp(-gr * gr * 8.0) * 0.9;

            float w = (g == 0) ? 1.0 : exp(-1.5 * gf) * 0.7;
            float amt = (fall * fil + core) * w;

            // Hue winds around the arm angle and drifts along the tail.
            vec3 col = fluxGradient(pingPong(gAng / kTau + gf * 0.3 + spin * 0.04));
            cloudC += col * amt;
            cloudA += amt;
        }

        cloudC *= cloudGain * 0.45;
        cloudA = clamp(cloudA * cloudGain * 0.4, 0.0, 1.0);
        outC.rgb += cloudC;
        outC.a = clamp(outC.a + cloudA, 0.0, 1.0);
    }

    outC.rgb = clamp(outC.rgb, 0.0, 1.0);
    return outC;
}
