// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Stretch vertex shader — grid-deformed elastic ("rubber-band") window-move.
//
// The leading edge springs into the destination zone first while the body
// lags behind, so the window stretches taut along its travel axis, tapers
// perpendicular (like taffy), then snaps to fit with a single overshoot.
// Entirely in-plane — no faked depth — but it needs the grid because the
// stretch varies across the window (a 4-vertex quad could only scale
// uniformly). Runs on the same window-relative grid as the other geometry
// effects: apply() builds an NxN grid over the padded composite canvas
// with destination-frame-relative texcoords
// (metadata `geometryGrid`) and this stage displaces each vertex.
//
// Each vertex rides a back-ease (easeOutBack) that overshoots its target
// once and settles, staggered along the travel axis so leading vertices
// arrive ahead of trailing ones. A global perpendicular squash tent thins
// the window while it is in motion. Both vanish at t = 0 and t = 1, so the
// source and settled states are pristine.

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
// Per-vertex data for the fragment: .xy = sampling card uv, .z = old->new
// cross-fade.
layout(location = 1) out vec3 vStretch;

// easeOutBack: rises to 1 with a single overshoot near the end, settling
// exactly at 1. p_overshoot scales the spring: 1.0 gives the classic ~10%
// overshoot; it was the baked OVERSHOOT = 1.2 constant (a slightly firmer
// snap) before it was declared.
float backOut(float x) {
    float c1 = 1.70158 * p_overshoot;
    float c3 = c1 + 1.0;
    float xm = x - 1.0;
    return 1.0 + c3 * xm * xm * xm + c1 * xm * xm;
}

void main() {
    const float PI = 3.14159265358979;
    // p_spread / p_squash were the baked SPREAD = 0.45 (trailing lag) and
    // SQUASH = 0.16 (perpendicular thinning) constants before they were
    // declared; both clamped to their declared ranges — an unbounded spread
    // pushes the stagger start past 1 (flow's guard), and a squash past 1
    // inverts the thinning into a stretch.
    float squash = clamp(p_squash, 0.0, 0.4);

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

    // Travel/perp basis from the leg's RIGID translation, not its centre
    // delta: a window pinned at one edge and grown moves its centre by half
    // the size change while standing still. legDirection falls back to the
    // growth axis, so an anchored stretch still orients along the axis that
    // actually changed.
    vec2 dir = legDirection(iFromRect, iToRect);
    vec2 perp = vec2(-dir.y, dir.x);

    // Endpoint centres: the spring below carries the card's centre from one
    // to the other, which stays correct for a resize (the centre genuinely
    // does move when one edge is pinned) — it is only the DIRECTION and the
    // stagger that must not be read off that motion.
    vec2 fromC = iFromRect.xy + 0.5 * iFromRect.zw;
    vec2 toC = iToRect.xy + 0.5 * iToRect.zw;

    // The staggered spring IS the stretch, and a stretch is a transit
    // character: it describes a window being dragged taut behind its leading
    // edge. An anchored resize has no leading edge to trail, and at full
    // strength the stagger makes parts of one window reach their final extent
    // at different times — a tear, not a stretch. Scale the stagger by the
    // travel share so a pure resize springs uniformly (the back-ease bounce
    // and the perpendicular squash both survive, since they ride the centre).
    float spread = clamp(p_spread, 0.0, 0.8) * legTravelShare(iFromRect, iToRect);

    // Per-vertex staggered spring: leading edge (s = 1) starts at t = 0,
    // trailing edge (s = 0) lags by `spread`; each rides a back-ease that
    // overshoots once. The spread of progress across the window IS the
    // stretch.
    float s = clamp(dot(cuv - 0.5, dir) + 0.5, 0.0, 1.0);
    float denom = max(1.0 - spread, 1.0e-3);
    float localLin = clamp((tt - (1.0 - s) * spread) / denom, 0.0, 1.0);
    float e = backOut(localLin);

    // Window centre rides the same spring (sampled at the mid stagger) so
    // the perpendicular squash thins around a consistent axis.
    float eC = backOut(clamp((tt - 0.5 * spread) / denom, 0.0, 1.0));
    vec2 centerPos = mix(fromC, toC, eC);

    // Per-vertex along-travel position (stretched), then thin perpendicular.
    // Split translation from extent (fold's guard, same hazard): the back-
    // ease overshoot rides the per-vertex CENTRE translation (which is the
    // stagger stretch and the bounce), while the vertex's offset within the
    // rect interpolates on a clamped copy — an extrapolated extent factor
    // goes negative on an extreme shrink (destination span under ~12% of the
    // source) and locally mirrors the card at the overshoot peak.
    vec2 fromPos = iFromRect.xy + cuv * iFromRect.zw;
    vec2 toPos = iToRect.xy + cuv * iToRect.zw;
    vec2 fromRel = fromPos - fromC;
    vec2 toRel = toPos - toC;
    vec2 alongPos = mix(fromC, toC, e) + mix(fromRel, toRel, clamp(e, 0.0, 1.0));

    vec2 rel = alongPos - centerPos;
    float alongComp = dot(rel, dir);
    // sin(PI * tt) goes negative past the ends, which would invert the squash into
    // a stretch on the tail of an overshoot. The envelope has no meaning outside the
    // leg; the intended overshoot rides on the rect, not on this.
    float perpComp = dot(rel, perp) * (1.0 - squash * sin(PI * clamp(tt, 0.0, 1.0)));
    vec2 finalPos = centerPos + dir * alongComp + perp * perpComp;

    vTexCoord = cuv;
    vStretch = vec3(cuv, clamp(eC, 0.0, 1.0));
#ifdef PLASMAZONES_KWIN
    // quad-space <-> screen-space is a pure 1:1 translation, so the screen
    // delta from this vertex's settled position applies straight to it.
    vec2 displaced = position + (finalPos - toPos);
    gl_Position = modelViewProjectionMatrix * vec4(displaced, 0.0, 1.0);
#else
    // Qt-RHI path: logical-px delta to clip units at 2 / iResolution, on
    // the pack's geometryGrid mesh — see flow's #else arm for the full
    // contract.
    vec2 rhiDisplaced = position + (finalPos - toPos) * 2.0 / max(iResolution, vec2(1.0));
    gl_Position = qt_Matrix * vec4(rhiDisplaced, 0.0, 1.0);
#endif
}
