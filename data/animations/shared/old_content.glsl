// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared old-content sampler for cross-fade move/resize transitions.
//
// The kwin-effect paint pipeline binds a snapshot of the window's OLD content
// (uOldWindow) so a transition can cross-fade the captured old frame into the
// live new content as the move settles. oldColor() samples that snapshot at a
// card-space uv, mirroring surfaceColor's iAnchorRectInTexture fold + KWin Y-up
// flip + iWindowOpacity multiply so old and new align and a SetOpacity rule dims
// both equally through the transition. This lived verbatim in every cross-fade
// frag (flow / fold / ripple-snap / stretch / window-morph); hoisted here so the
// fallback logic has one source of truth.
//
// Daemon builds define no PLASMAZONES_KWIN, so this whole unit compiles away —
// the daemon transition paths never sample old content. Include AFTER the
// animation uniform block so iHasOldWindow / iAnchorRectInTexture / iWindowOpacity
// and surfaceColor() are in scope.
#ifdef PLASMAZONES_KWIN
uniform sampler2D uOldWindow;

vec4 oldColor(vec2 uv) {
    // No captured old frame (snapshot-less lifecycle transitions, e.g.
    // window.move at drag start): fall back to the live decorated surface so
    // the cross-fade runs decorated-to-decorated. Sampling the unit-0 alias
    // here would show the RAW window and blank every decoration pack until
    // the fade completes.
    if (iHasOldWindow == 0) {
        return surfaceColor(uv);
    }
    vec2 t = iAnchorRectInTexture.xy + uv * iAnchorRectInTexture.zw;
    return texture(uOldWindow, vec2(t.x, 1.0 - t.y)) * iWindowOpacity;
}
#endif
