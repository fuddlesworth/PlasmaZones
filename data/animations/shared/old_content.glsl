// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared old-content sampler for cross-fade move/resize transitions.
//
// The kwin-effect paint pipeline binds a snapshot of the window's OLD content
// (uOldWindow) so a transition can cross-fade the captured old frame into the
// live new content as the move settles. oldColor() samples that snapshot at a
// card-space uv, mirroring surfaceColor's iAnchorRectInTexture fold + KWin
// Y-up flip, and dims by the old side's OWN resolved opacity
// (iOldWindowOpacity — equal to iWindowOpacity for a self-cross-fade, the
// outgoing tab's value on the tab class). The sampler + fallback logic lived
// verbatim in the cross-fade frags (flow / fold / phosphor-stream /
// ripple-snap / stretch); hoisted here so it has one source of truth.
//
// Old-content cross-fades are compositor-only: the packs that include this
// module are excluded from the daemon's SPIR-V bake entirely, so the sampler
// is declared unguarded. Include AFTER the
// animation uniform block so iHasOldWindow / iAnchorRectInTexture / iWindowOpacity
// and surfaceColor() are in scope.
#ifndef PLASMAZONES_OLD_CONTENT_GLSL
#define PLASMAZONES_OLD_CONTENT_GLSL

uniform sampler2D uOldWindow;

// The OLD side's own resolved opacity, distinct from iWindowOpacity on
// purpose: on the tab class the snapshot holds a DIFFERENT window (the
// outgoing tab), and multiplying it by the arriving window's opacity dimmed
// the wrong side whenever a SetOpacity rule distinguished the two tabs. For
// every self-cross-fade includer the C++ pushes the same value into both
// uniforms, so their behaviour is bit-identical to when oldColor read
// iWindowOpacity directly.
uniform float iOldWindowOpacity;

vec4 oldColor(vec2 uv) {
    // No captured old frame (snapshot-less lifecycle transitions, e.g.
    // window.move at drag start): fall back to the live decorated surface so
    // the cross-fade runs decorated-to-decorated. Sampling the unit-0 alias
    // here would show the RAW window and blank every decoration pack until
    // the fade completes.
    //
    // Packs that sample old and new at DIFFERENT coordinates (slide, zoom,
    // flip) must NOT rely on this fallback — it substitutes at the displaced
    // uv and draws a duplicate of the live content. They carry their own
    // iHasOldWindow bail at the top of pTransition instead.
    if (iHasOldWindow == 0) {
        return surfaceColor(uv);
    }
    vec2 t = iAnchorRectInTexture.xy + uv * iAnchorRectInTexture.zw;
    return texture(uOldWindow, vec2(t.x, 1.0 - t.y)) * iOldWindowOpacity;
}

// The old→new blend, `a` running 0 (all old) to 1 (all new) — the sibling of
// desktop_transition.glsl's crossFade, named apart from it because the pair
// it blends is different (a window's captured past against its live present,
// rather than two desktops). A helper for packs whose blend IS this one mix
// (tab-wipe, tab-dissolve); the older cross-fade frags predate it and keep
// their hand-written equivalents, and phosphor-transfer deliberately inlines
// it to reuse the fetched samples.
//
// The tab class is the caller this matters most to: there the "old" side is
// another WINDOW's content, so which sampler is the from and which is the to
// is a fact about the event rather than something a pack should restate.
vec4 oldCrossFade(vec2 uv, float a) {
    return mix(oldColor(uv), surfaceColor(uv), a);
}

#endif // PLASMAZONES_OLD_CONTENT_GLSL
