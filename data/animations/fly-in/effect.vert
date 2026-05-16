// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fly-in vertex shader — translates the surface horizontally on enter so it
// appears to fly in from the screen edge nearest to its current resting
// position. The "closest horizontal edge" decision is made per-frame from
// `iSurfaceScreenPos` (.xy = card screen origin, .zw = screen size):
//   • cardCenterX = iSurfaceScreenPos.x + iResolution.x * 0.5
//   • leftDist  = cardCenterX
//   • rightDist = iSurfaceScreenPos.z - cardCenterX
//   • dirSign = (leftDist < rightDist) ? -1 (fly from left) : +1 (fly from right)
//
// At iTime=0 the geometry is offset by `clearancePx` toward the closer
// edge so the card sits fully off-screen; the offset shrinks linearly to
// zero at iTime=1. This shader REQUIRES `"fboExtent": "surface"` in
// metadata.json on the daemon path; without that the FBO is anchor-
// sized and the translated geometry walks straight off the FBO edge,
// producing the "flies in from the anchor's location" symptom that
// motivated the surface-extent contract in the first place.
//
// Coordinate-space split (daemon vs kwin):
//
//  • Daemon (Qt RHI): with fboExtent=surface the FBO covers the
//    QQuickWindow's contentItem (the wl_surface scene root, sized to
//    the host screen / VS rect for OSDs and popups), and `iResolution`
//    naturally tracks that FBO size (Qt auto-syncs it from the shader
//    item's bounds). Card pixel size comes from `iAnchorSize`. The
//    vertex shader has to remap the standard (-1..1) clip-space quad
//    onto the card's region within the surface-sized FBO before
//    applying translation; that's the bulk of the math below. Position
//    arrives in clip space directly (no MVP).
//
//  • kwin-effect (classic GL): position is in window-frame logical
//    pixels, `modelViewProjectionMatrix` carries the window-to-screen
//    translation, `iResolution` matches the window frame (= the visible
//    card on this path), and the FBO is the redirected window texture's
//    full bounds. Adding `offsetPx` to position.x directly translates
//    the rendered window across the screen — no remap needed. iTexCoord
//    is also Y-flipped here (kwin's FBO is Y-up, daemon's Qt scene
//    coords are Y-down).

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
#endif

void main() {
    // texCoord — kwin's Y is up in the FBO, so flip; daemon's Qt scene
    // coords already have Y down. Mirrors `kKwinDefaultVertexSource` /
    // `kDefaultVertexShaderSource` from the C++ side.
#ifdef PLASMAZONES_KWIN
    vTexCoord = vec2(texCoord.x, 1.0 - texCoord.y);
#else
    vTexCoord = texCoord;
#endif

    // Horizontal translation amount, monotonically reducing 1 → 0 over
    // iTime. Clamp because bouncy curves overshoot iTime past 1 — we
    // already clamp at the C++ source on the daemon side, but a hide
    // leg flipping iTime 1→0 can also dip negative on overshoot.
    float t = clamp(iTime, 0.0, 1.0);
    float remaining = 1.0 - t;

    // Decide direction from card position on its host screen. Card size
    // comes from `iAnchorSize` (NOT `iResolution`): the latter is auto-
    // reset by Qt to the shader item's bounds on every geometry event,
    // which under fboExtent=surface means the entire wl_surface, so
    // reading iResolution here would produce a screen-sized card during
    // motion. iAnchorSize is the runtime-pushed captured-anchor size
    // and stays stable per leg.
    //
    // Default to "fly from left" when iSurfaceScreenPos hasn't been
    // populated yet (.zw = 0 means the runtime didn't push a screen
    // size — typically the first frame on the daemon path before the
    // window's compositor configure). This keeps the shader visible
    // and recognisable rather than producing a NaN division below.
    float screenW = max(iSurfaceScreenPos.z, 1.0);
    vec2 cardSize = vec2(max(iAnchorSize.x, 1.0), max(iAnchorSize.y, 1.0));
    float cardCenterX = iSurfaceScreenPos.x + cardSize.x * 0.5;
    float leftDist = cardCenterX;
    float rightDist = screenW - cardCenterX;
    float dirSign = (leftDist < rightDist) ? -1.0 : 1.0;

    // Distance to push — full clearance off the near screen edge. For
    // "fly from left", that's the card's left edge plus its width worth
    // of leftward travel (so the card's right edge clears the screen
    // origin). For "fly from right", screenW - cardLeft pixels of
    // rightward travel.
    float clearancePx = (dirSign < 0.0)
        ? max(iSurfaceScreenPos.x + cardSize.x, 1.0)
        : max(screenW - iSurfaceScreenPos.x, 1.0);
    float offsetPx = dirSign * remaining * clearancePx;

#ifdef PLASMAZONES_KWIN
    // kwin: position is in window-frame logical pixels; add the offset
    // directly, then run through the MVP matrix.
    vec2 shifted = position + vec2(offsetPx, 0.0);
    gl_Position = modelViewProjectionMatrix * vec4(shifted, 0.0, 1.0);
#else
    // Daemon (fboExtent=surface): the FBO spans the QQuickWindow's
    // contentItem (host screen / VS rect post-fullscreen-OSD
    // migration). Map the standard (-1..1) clip-space quad onto the
    // card's region within the FBO so the captured anchor texture
    // renders at native pixel size at the card's resting screen
    // position, then add the fly-in offset.
    //
    // CRITICAL: use `iSurfaceScreenPos.zw` (logical pixels) — NOT
    // `iResolution` — for the FBO-size denominator. iResolution is
    // auto-synced by Qt from the shader item's geometry events; on
    // the FIRST frame of a leg attached against a fresh-from-warmup
    // Window (login / reboot), the geometry-change signal that
    // would resize iResolution to the screen-sized FBO has not yet
    // propagated. Reading iResolution there returns the QML
    // Window-default size (15×4 gridUnits = ~210×60 px), and the
    // clip-space math below collapses to placing the card at clip
    // coords far outside [-1, 1], cropping the rendered output to
    // a sliver at the FBO edge — the visible "OSD displayed at the
    // very top, almost cut off" symptom reported at login. The
    // `iSurfaceScreenPos.zw` field is pushed synchronously by
    // SurfaceAnimator::syncShaderGeometryNow from the scene root's
    // bounds and is fresh on the very first frame of every leg.
    vec2 fboSizePx = vec2(max(iSurfaceScreenPos.z, 1.0), max(iSurfaceScreenPos.w, 1.0));

    // Card centre in clip space. Qt's QSGRenderNode convention matches
    // the screen: clip-space Y = -1 is the top of the FBO and Y = +1
    // is the bottom (same Y-down as the captured texture's UV). No flip
    // here; a card at screen Y near 0 should land at clip-Y near -1 and
    // a card near the bottom of the screen should land at clip-Y near +1.
    vec2 cardCenterPx = iSurfaceScreenPos.xy + cardSize * 0.5;
    vec2 cardCenterClip;
    cardCenterClip.x = (cardCenterPx.x / fboSizePx.x) * 2.0 - 1.0;
    cardCenterClip.y = (cardCenterPx.y / fboSizePx.y) * 2.0 - 1.0;

    // Card half-size relative to FBO, also in clip-space units.
    vec2 cardHalfClip = cardSize / fboSizePx;

    // Map (-1..1) input clip-space onto the card's clip-space region.
    vec2 cardClipPos = cardCenterClip + position * cardHalfClip;

    // Fly-in translation, pixels → clip units (× 2 / fboSize).
    cardClipPos.x += offsetPx * (2.0 / fboSizePx.x);

    gl_Position = vec4(cardClipPos, 0.0, 1.0);
#endif
}
