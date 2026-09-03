// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Siphon vertex shader — the Phosphor set's minimize pack: the
// window is siphoned into its task-manager icon as separated luminous
// streams.
//
// Genie's icon-targeted grid mechanics (the grid sits on the window's
// frame rect, or its padded decoration canvas with frame-relative
// texcoords; each vertex travels from its place in the window to its
// place in iIconRect, staggered so the icon-facing edge leads) crossed
// with phosphor-stream's LANES: the card is cut into strips parallel to
// the travel axis, each lane runs its own slightly offset clock and bows
// perpendicular to the motion mid-flight. Lag and bow are both exactly
// zero at e = 0 and e = 1, so the resting window carries no residue and
// every stream lands inside the icon by the leg's end.
//
// Dual-branch: the deformation runs on BOTH uniform ABIs. The kwin path
// declares iIconRect as a default-block uniform; the UBO branch reads it
// from the AnimationUniforms transition tail, where a preview host stands
// in a bottom-of-field target. The math between the two is shared
// verbatim (same structure as genie).
//
// Direction handling: the host flips iTime on the reverse
// (going-to-minimized) leg, so `1 - iTime` is the siphon progress on BOTH
// legs — 0 → 1 while minimizing, 1 → 0 while restoring. No iIsReversed
// branch is needed (same convention as genie).
//
// Coordinate space: iSurfaceScreenPos.xy + iAnchorSize give the window's
// frame rect in the same global logical pixels as iIconRect, and
// quad-space <-> screen-space is a pure translation at 1:1 logical scale,
// so a screen-space displacement applies directly to `position`.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
// Task-manager icon rect (logical screen px, x/y/w/h), pushed by the
// kwin-effect paint pipeline for any shader that declares it.
// (0, 0, 0, 0) means the window sits in no task manager. The UBO branch
// reads the block member of the same name instead.
uniform vec4 iIconRect;
#endif
// Per-vertex siphon state handed to the fragment: .xy = card uv, .z =
// arrival ease (0 = at rest in the window, 1 = inside the icon), .w =
// lane seed.
layout(location = 1) out vec4 vSiphon;

float laneHash(float n) {
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

void main() {
#ifdef PLASMAZONES_KWIN
    // Card uv with y = 0 at the window top (KWin Y-flips window-quad
    // texcoords on upload; re-apply the flip, same as flow).
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);
#else
    // The Qt-RHI quad's texCoord is Y-down already — no re-flip.
    vec2 cuv = texCoord;
#endif

    // Window frame rect in global logical px — the same space iIconRect
    // is pushed in.
    vec4 W = vec4(iSurfaceScreenPos.xy, iAnchorSize);

    // Icon target. A window with no task-manager entry drains into a
    // sliver on its own bottom edge instead, so the animation still
    // reads as the window going away downward.
    vec4 I = iIconRect;
    if (I.z < 1.0 || I.w < 1.0) {
        // Capped at the window's own width so a very narrow window's
        // sliver cannot poke past its edges. Twin site:
        // genie/effect.vert (same fallback, same rationale).
        float sw = min(48.0, W.z);
        I = vec4(W.x + 0.5 * (W.z - sw), W.y + W.w - 6.0, sw, 6.0);
    }

    // Siphon progress, both legs (see header comment).
    float p = 1.0 - clamp(iTime, 0.0, 1.0);

    // Travel direction in screen space (y-down). A degenerate target
    // (icon centred on the window) defaults to downward.
    vec2 wc = W.xy + 0.5 * W.zw;
    vec2 ic = I.xy + 0.5 * I.zw;
    vec2 travel = ic - wc;
    vec2 dir = (dot(travel, travel) > 1.0) ? normalize(travel) : vec2(0.0, 1.0);
    vec2 perp = vec2(-dir.y, dir.x);

    // Phase along the travel axis: the icon-facing edge is 1 and leads,
    // the far edge is 0 and lags.
    float phase = clamp(dot(cuv - 0.5, dir) + 0.5, 0.0, 1.0);

    // Lane identity: strips parallel to the travel axis. The hash offsets
    // each lane's clock and bow so the streams read as individual pours.
    float laneCount = clamp(p_lanes, 2.0, 24.0);
    float lane = floor(dot(cuv - 0.5, perp) * laneCount + 0.5);
    float lh = laneHash(lane);

    // Staggered local progress: trailing regions lag by `spread`, each
    // lane adds its own hash lag on top. The lane lag is scaled by the
    // headroom LEFT OVER after `spread`, so spread + laneLag can never
    // reach 1.0 and every vertex arrives by the leg's end (same headroom
    // rationale as phosphor-stream).
    float spread = clamp(p_spread, 0.0, 0.8);
    float laneLag = clamp(p_streamLag, 0.0, 1.0) * (1.0 - spread) * 0.4;
    float startT = (1.0 - phase) * spread + lh * laneLag;
    float localT = clamp((p - startT) / max(1.0 - spread - laneLag, 1.0e-3), 0.0, 1.0);
    float e = localT * localT * (3.0 - 2.0 * localT);

    // Each card point travels from its place in the window to its place
    // in the icon, plus the lane bow: streams arc apart perpendicular to
    // the motion, peaking mid-flight and exactly zero at both endpoints.
    vec2 natural = W.xy + cuv * W.zw;
    vec2 target = I.xy + cuv * I.zw;
    vec2 delta = (target - natural) * e;
    float mid = e * (1.0 - e) * 4.0;
    delta += perp * (lh - 0.5) * 2.0 * max(p_bow, 0.0) * mid;

    vTexCoord = cuv;
    vSiphon = vec4(cuv, e, lh);
#ifdef PLASMAZONES_KWIN
    gl_Position = modelViewProjectionMatrix * vec4(position + delta, 0.0, 1.0);
#else
    // Qt-RHI path: logical-px delta converts to clip units at
    // 2 / iResolution — see flow's #else arm for the full contract.
    gl_Position = qt_Matrix * vec4(position + delta * 2.0 / max(iResolution, vec2(1.0)), 0.0, 1.0);
#endif
}
