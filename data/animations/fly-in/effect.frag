// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Fly-in transition — the window slides in from the horizontal screen
// edge nearest its resting position. The offset shrinks linearly to
// zero over the leg.
//
// SURFACE EXTENT — this is a `fboExtent: "surface"` shader: the runtime
// sizes the render target to the whole surface (the daemon's scene
// root, or the window's output under the kwin-effect's apply()
// override) so the window can genuinely travel off its frame.
// `anchorRemap` (see anchor_remap.glsl) converts each fragment's
// surface-UV into the window's own [0,1] space; the window is then
// rigidly translated horizontally and `boundaryMaskAA` (see noise.glsl)
// antialiases the crop one device pixel wide so the daemon's anchor-
// sized texture leaves no clamp-to-edge halo.
//
// The earlier fly-in translated the quad geometry in the vertex stage.
// That worked on the daemon but not the kwin-effect: apply() expands
// the kwin quad to cover the whole output, so a passthrough fragment
// sampler stretched the window texture across the entire output — the
// "window balloons to full screen while sliding in" symptom. Doing the
// translate in fragment space via `anchorRemap` is the contract
// broken-glass / morph / bounce already use and behaves identically on
// both runtimes.
//
// DIRECTION — the closer horizontal edge is chosen from the window's
// position WITHIN its render target: `iAnchorPosInFbo` (render-target-
// relative origin), `iResolution` (render-target size) and
// `iAnchorSize` (the window's pixel size). All three share one
// coordinate space on every output and on both runtimes.
// `iSurfaceScreenPos.xy` is deliberately NOT used here — it is a global
// screen coordinate and on a non-primary output does not share an
// origin with a per-output width.
//
// PlasmaZones flips iTime on reverse legs (0→1 on open, 1→0 on close),
// so the close leg slides back out automatically — no reversed branch.

#include <noise.glsl>
#include <anchor_remap.glsl>

vec4 pTransition(vec2 uv, float t) {
    // Clamp: bouncy easing curves can drive iTime past [0, 1], and a
    // reverse leg flipping 1→0 can dip negative on overshoot.
    t = clamp(t, 0.0, 1.0);
    float remaining = 1.0 - t;

    // Surface-spanning UV → the window's own ("anchor") [0,1] space.
    // Fragments outside the window map outside [0,1] and are cropped by
    // boundaryMaskAA below.
    vec2 auv = anchorRemap(uv);

    // Decide the fly-in edge from the window's position within its
    // render target. `iAnchorPosInFbo` (render-target-relative origin)
    // and `iResolution` (render-target size) share one coordinate space
    // on every output; `iSurfaceScreenPos.x` is a GLOBAL screen
    // coordinate, so on a non-primary output it does not share an origin
    // with the per-output width and mixing the two flips the chosen edge
    // for windows on secondary monitors. `iAnchorSize` is the window
    // pixel size (NOT iResolution, which is the surface-sized FBO under
    // fboExtent=surface).
    //
    // `fboW` rather than "screenW" because the value is the FBO width by
    // contract — the surface-extent path happens to size its FBO to the
    // host output, so screen and FBO coincide here, but the variable
    // tracks the uniform's actual semantic so a future reader doesn't
    // "fix" this to `iSurfaceScreenPos.z` (the comment block above
    // explicitly bans that uniform from this calculation).
    float fboW = max(iResolution.x, 1.0);
    vec2 cardSize = vec2(max(iAnchorSize.x, 1.0), max(iAnchorSize.y, 1.0));
    float cardLeft = iAnchorPosInFbo.x;
    float cardCenterX = cardLeft + cardSize.x * 0.5;
    float dirSign = (cardCenterX < fboW - cardCenterX) ? -1.0 : 1.0;

    // Pixels needed to push the window fully clear of the nearer edge:
    // its full on-screen extent toward that edge. `max(.., 1.0)` guards
    // a first frame before the runtime has populated the geometry.
    float clearancePx = (dirSign < 0.0)
        ? max(cardLeft + cardSize.x, 1.0)
        : max(fboW - cardLeft, 1.0);

    // Screen-space horizontal translation, shrinking 1→0 over the leg,
    // expressed in window-widths so it applies directly in anchor space.
    float offsetUv = dirSign * remaining * clearancePx / cardSize.x;

    // Rigid horizontal translate: a window shifted toward +x on screen
    // is sampled further toward -x in its own texture.
    vec2 sample_uv = auv;
    sample_uv.x = auv.x - offsetUv;
    return surfaceColor(sample_uv) * boundaryMaskAA(sample_uv);
}
