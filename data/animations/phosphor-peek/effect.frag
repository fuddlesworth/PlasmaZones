// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Peek — the show-desktop peek in the official Phosphor style, the
// peek leg of the phosphor-flux / phosphor-bloom / desktop-phosphor set. The
// windows scene de-energizes like a powered circuit shutting down: a front
// sweeps across the screen along glowing traces, the windows drain out along
// those traces into the brand gradient (cyan #22D3EE, blue #3B82F6, purple
// #A855F7, rose #F43F5E), and the bare desktop is left behind the front.
//
// `t` is forward peek progress in [0,1]: the FROM texture is the scene with
// windows, the TO texture is the bare desktop. The kwin-effect swaps the two
// textures for the show-back leg, so this shader only ever animates from the
// windows scene toward the desktop and needs no direction or reversal logic.
// Run by the screen-level desktop-transition pass, which binds uFromDesktop and
// uToDesktop, pushes progress as iTime, and binds the p_color* slots from the
// customColors pool at parity with the per-window and surface contracts.
#include <desktop_transition.glsl>
#include <noise.glsl>

const float PZ_PI = 3.141592653589793;

// Four-stop brand gradient, t in [0, 1]: cyan -> blue -> purple -> rose. Each
// stop falls back to its brand default when its colour slot is unset.
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

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    float tt = clamp(t, 0.0, 1.0);
    vec2 res = resolutionSafe();
    float aspect = res.x / max(res.y, 1.0);
    float density = max(p_scale, 2.0);

    // Drain direction. iSwitchDelta is all zeros for a peek, so the configured
    // direction is used directly; the default (1, 1) drains along the brand's
    // top-left-to-bottom-right diagonal. Guard the degenerate zero vector the
    // way desktop_transition.glsl's switchDirection() does — an epsilon nudge
    // alone still normalizes to NaN for tiny opposing components.
    vec2 rawDir = vec2(p_dirX, p_dirY);
    vec2 dir = dot(rawDir, rawDir) > 1.0e-6 ? normalize(rawDir) : vec2(1.0, 0.0);
    vec2 perp = vec2(-dir.y, dir.x);

    // Position of this fragment projected onto the drain direction, in [0,1].
    float proj = clamp(dot(uv - 0.5, dir) * 0.7071 + 0.5, 0.0, 1.0);
    // Meander the front along the traces so it reads as signal drain, not a
    // straight wipe. The jitter is bounded so the endpoint guarantees hold.
    float jitter = (fbm(uv * density * vec2(aspect, 1.0), 4, 2.0) - 0.5) * 0.18;
    float p = proj + jitter;

    // The front sweeps from well before 0 to well past 1 so that t = 0 is
    // exactly the windows scene and t = 1 is exactly the desktop, whatever the
    // softness and jitter do in between.
    float soft = mix(0.05, 0.4, clamp(p_softness, 0.0, 1.0));
    float front = mix(-0.6, 1.6, tt);

    // reveal: 0 = windows still here .. 1 = desktop drained through.
    float reveal = smoothstep(p - soft, p + soft, front);
    reveal = max(reveal, smoothstep(0.96, 1.0, tt));

    vec3 col = crossFade(uv, reveal).rgb;

    // Global envelope so the additive glow is exactly 0 at both endpoints and
    // never lingers on the settled frames.
    float env = smoothstep(0.0, 0.05, tt) * (1.0 - smoothstep(0.9, 1.0, tt));

    // Distance from the front, signed: positive where the front has passed.
    float fb = front - p;
    // A band that peaks right on the front where the drain happens.
    float band = smoothstep(-0.28, 0.0, fb) * (1.0 - smoothstep(0.0, 0.28, fb));

    // De-energise: the windows darken just as the front reaches them, before
    // the desktop takes over. Gated by band, so no residue at the endpoints.
    col *= 1.0 - band * (1.0 - reveal) * clamp(p_dim, 0.0, 1.0) * 0.7;

    // Traces: thin lines parallel to the drain direction, carrying dashes that
    // flow along the drain so the windows read as pixels draining down them.
    float across = dot(uv - 0.5, perp);
    float freq = density * 2.0;
    float tri = abs(fract(across * freq) - 0.5) * 2.0;
    float line = 1.0 - smoothstep(0.0, 0.12, tri);
    float dash = fract(proj * freq - float(iFrame) * 0.05
                       + classicHash(floor(vec2(across * freq, 0.0))));
    float flow = smoothstep(0.6, 1.0, dash);
    float trace = line * (0.35 + 0.65 * flow);

    // Draining sparks: brand-coloured points twinkling on the front.
    float spark = classicHash(floor(uv * res / 6.0) + floor(float(iFrame) * 0.35));
    spark = smoothstep(0.92, 1.0, spark);

    // Brand-gradient glow riding the front, coloured by how far along the drain
    // the local front is, brightened along the traces and by the sparks.
    vec3 frontCol = fluxGradient(clamp(p, 0.0, 1.0));
    float glow = clamp(p_glow, 0.0, 2.0);
    col += frontCol * band * env * glow * (0.45 + 0.9 * trace);
    col += frontCol * band * env * glow * spark * 0.8;

    // Two opaque scenes blended stay opaque; the pass draws with blending off
    // and replaces the screen, so alpha is a constant 1.
    return vec4(clamp(col, 0.0, 1.0), 1.0);
#else
    // Desktop transitions are compositor-only; the daemon never runs them.
    // Return transparent so the pack still bakes for the daemon target.
    return vec4(0.0);
#endif
}
