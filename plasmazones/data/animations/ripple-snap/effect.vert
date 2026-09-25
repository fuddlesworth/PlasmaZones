// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Ripple-snap vertex shader — grid-deformed impact window-move.
//
// The window accelerates rigidly into its destination zone (a "slam"),
// then on arrival a decaying wave travels across the grid from the leading
// edge — the edge that hit the zone boundary — like a sheet snapping taut.
// Runs on the same window-relative grid as flow/fold: apply() builds an
// NxN grid over the padded composite canvas with destination-frame-relative texcoords
// (metadata `geometryGrid`) and
// this stage displaces each vertex.
//
// The wave is a transverse (in-plane, perpendicular-to-travel) ripple so
// it is visible head-on under KWin's orthographic projection, plus a
// crest-shade factor for depth cueing. It is zero during travel, peaks at
// impact, and decays to flat at the destination.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
// Geometry-morph endpoints (logical-screen px, x/y/w/h), pushed by the
// kwin-effect paint pipeline for any shader that declares them. On the
// UBO branch both rects come from the AnimationUniforms transition tail.
uniform vec4 iFromRect;
uniform vec4 iToRect;
#endif
// Per-vertex data for the fragment: .xy = sampling card uv, .z = ripple
// shade, .w = old->new cross-fade.
layout(location = 1) out vec4 vRip;

void main() {
    const float TWO_PI = 6.28318530718;
    // p_impact / p_speed / p_waves / p_decay / p_amplitude / p_shade were
    // the baked IMPACT = 0.4, SPEED = 1.6, FREQ = 3.0, SPATIAL_DECAY = 3.5,
    // AMP = 0.06 and SHADE = 0.5 constants before they were declared. Only
    // impact needs a guard: it divides the travel phase, so a hand-edited
    // zero would NaN the slam; the others degrade gracefully (the shade
    // result is clamped below and again in effect.frag).
    float impact = clamp(p_impact, 0.1, 0.7); // fraction of the leg spent travelling
    float waveSpeed = p_speed;                // wavefront speed across the window
    float waveFreq = p_waves;                 // oscillations in the wave packet
    float spatialDecay = p_decay;             // falloff behind the wavefront
    float amp = p_amplitude;                  // transverse amplitude (card uv)
    float shadeGain = p_shade;                // crest/trough shade gain

    // Card uv with KWin's window-quad texcoord flip re-applied (see flow).
#ifdef PLASMAZONES_KWIN
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);
#else
    // The Qt-RHI quad's texCoord is Y-down already — no re-flip.
    // ...and spans the captured canvas: fold the anchor sub-rect so cuv is
    // card space (identity rect on a bare-card capture = passthrough).
    vec2 cuv = (texCoord - iAnchorRectInTexture.xy) / max(iAnchorRectInTexture.zw, vec2(1.0e-5));
#endif

    float tt = legProgress();

    // Travel/perp basis from the leg's RIGID translation rather than its
    // centre delta: a window pinned at one edge and grown moves its centre
    // by half the size change while standing still, which would send the
    // wavefront across an axis nothing travelled along. legDirection falls
    // back to the growth axis, so a resize ripples along the axis that
    // changed size. That fallback reads the size delta alone and cannot tell
    // which edge was held, so the wavefront runs along the positive axis on a
    // grow and the negative one on a shrink either way. The wave is NOT
    // damped for a resize the way the
    // staggered packs are: `rect` below interpolates rigidly, so the ripple
    // is an arrival wobble on a window that has already settled uniformly,
    // never parts of one window arriving at different times.
    vec2 dir = legDirection(iFromRect, iToRect);
    vec2 perp = vec2(-dir.y, dir.x);

    // Rigid travel: accelerate into the zone so the arrival reads as a
    // slam, then sit at the destination for the ripple phase.
    float te = clamp(tt / impact, 0.0, 1.0);
    float teE = te * te;
    vec4 rect = mix(iFromRect, iToRect, teE);

    // Ripple phase: a wavefront leaves the leading edge (proj = 1) and
    // crosses to the trailing edge, decaying behind the front and over time.
    float rp = clamp((tt - impact) / max(1.0 - impact, 1.0e-3), 0.0, 1.0);
    float proj = dot(cuv - 0.5, dir) + 0.5; // 0..1 along travel
    float d = 1.0 - clamp(proj, 0.0, 1.0);  // distance from the contact edge
    float x = rp * waveSpeed - d;           // >0 once the front has passed
    float osc = sin(x * waveFreq * TWO_PI);
    float spatialEnv = smoothstep(0.0, 0.08, x) * exp(-max(x, 0.0) * spatialDecay);
    float timeEnv = 1.0 - rp;               // settle flat by the end
    float wave = osc * spatialEnv * timeEnv;

    // Transverse in-plane displacement + map through the interpolated rect.
    vec2 duv = cuv + perp * (wave * amp);
    vec2 screenPos = rect.xy + duv * rect.zw;
    vec2 toPos = iToRect.xy + cuv * iToRect.zw;
    vec2 displaced = position + (screenPos - toPos);

    // Fade-only: brightening a premultiplied sample (scaling RGB and
    // alpha together) would push alpha past 1.0 in the FBO, so cap at
    // 1.0; the sub-1 side is the coverage fade the frag applies to the
    // troughs (see effect.frag).
    float shade = clamp(1.0 + wave * shadeGain, 0.6, 1.0);

    vTexCoord = cuv;
    vRip = vec4(cuv, shade, teE);
#ifdef PLASMAZONES_KWIN
    gl_Position = modelViewProjectionMatrix * vec4(displaced, 0.0, 1.0);
#else
    // Qt-RHI path: logical-px delta to clip units at 2 / iResolution, on
    // the pack's geometryGrid mesh — see flow's #else arm for the full
    // contract.
    vec2 rhiDisplaced = position + (screenPos - toPos) * 2.0 / max(iResolution, vec2(1.0));
    gl_Position = qt_Matrix * vec4(rhiDisplaced, 0.0, 1.0);
#endif
}
