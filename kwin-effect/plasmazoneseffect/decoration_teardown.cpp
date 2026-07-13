// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Decoration TEARDOWN, split out of decorations.cpp to keep that file under the
// 800-line limit. Every path that takes a decoration away lives here: the single
// per-window remove, the two GL release halves it delegates to (both guarded
// against tearing down state a live shader transition is still sampling), and the
// bulk clear used on genuine teardown (daemon lost, effect unloaded).
//
// NOT the focus/rule refresh path: updateAllDecorations reconciles in place and
// removes only what it did not revisit. See decorations.cpp.

#include "../plasmazoneseffect.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

#include "window_query.h"

namespace PlasmaZones {

// The EXACT window a decoration belongs to. One resolver, because the two sites that
// need it sit three lines apart and used to spell the invariant differently — and the
// weaker spelling (`w ? w : reverse`, with no exact-id re-check) would hand a fuzzy
// same-app sibling to the GL teardown the moment a caller passed a non-exact window.
//
// Preference order: an exact-id live match; else the frozen reverse mapping (which
// resolves a DELETED window under its exact id and survives a close transition); else
// the caller's hint. The hint comes last because the close path passes the corpse, whose
// getWindowId no longer re-derives to windowId and so cannot be exact-checked here.
KWin::EffectWindow* PlasmaZonesEffect::resolveDecorationTarget(const QString& windowId, KWin::EffectWindow* hint)
{
    KWin::EffectWindow* target = findWindowById(windowId);
    if (target && getWindowId(target) != windowId) {
        target = nullptr; // a fuzzy appId fallback resolved a SIBLING; not ours
    }
    if (!target) {
        target = m_windowIdReverse.value(windowId);
    }
    return target ? target : hint;
}

void PlasmaZonesEffect::removeWindowDecoration(const QString& windowId, KWin::EffectWindow* windowHint,
                                               bool keepSurfaceState)
{
    auto it = m_windowDecorations.find(windowId);
    if (it == m_windowDecorations.end()) {
        return;
    }
    WindowDecoration& wb = it.value();
    // Resolve the EXACT window this decoration belongs to, once, for both the
    // GL teardown and the multipass-guard below. findWindowById's fuzzy appId
    // fallback can return a same-app SIBLING for a dead windowId, and tearing
    // down the sibling's redirect / erasing its FBO would be wrong — the
    // callers (reconcileDecorationOnPlacementFlip, the windowDecorationRestored
    // handler) re-check getWindowId(w) == windowId precisely to avoid this, but
    // this internal re-resolution must too. Preference order: an exact-id live
    // match; else the frozen reverse mapping (resolves a deleted window under
    // its exact id, and survives a close transition); else the caller's hint
    // (the close path passes the corpse, whose getWindowId no longer
    // re-derives to windowId, so it can't be exact-checked here).
    KWin::EffectWindow* const target = resolveDecorationTarget(windowId, windowHint);
    // Hand the offscreen redirect + shader slot back IF this decoration owns it.
    // (releaseDecorationGl no-ops while a transition owns the slot: the transition
    // lifecycle owns that handover, and a stray unredirect would tear down the
    // animation mid-flight.)
    //
    // Running it on the DELETED-but-still-painted window is deliberate and
    // load-bearing: KWin keys both calls on its redirected-windows map and the
    // redirect stays live until the window is discarded. Skipping it left the corpse
    // redirected with the present shader still bound while the surface-state erase
    // below destroyed the composite textures its samplers referenced — GL auto-unbinds
    // a deleted texture, an unbound sampler reads opaque black, and the next paint of
    // the Deleted drew the whole expanded quad black (the close flash).
    //
    // SKIPPED on the REFRESH path (keepSurfaceState), and that is the larger half of
    // what keeping the state buys. A refresh re-asserts the very redirect and shader
    // this would drop, so releasing here would: destroy KWin's OffscreenData (texture
    // AND framebuffer) only for the re-apply to make KWin allocate a fresh one, the
    // exact churn we are avoiding one layer up; and issue an addRepaintFull that is
    // NOT flagged m_selfRepainting, so the windowDamaged handler would clear
    // captureValid and cold-start the capture cache anyway. Both on every focus
    // change, for every decorated window. The three exits in updateWindowDecoration
    // where the refresh finds the window undecoratable run the release themselves.
    if (wb.shaderApplied && !keepSurfaceState) {
        releaseDecorationGl(target, wb.outerPadding);
    }
    if (wb.paddedGeoConnection) {
        disconnect(wb.paddedGeoConnection);
    }
    if (wb.damageConnection) {
        disconnect(wb.damageConnection);
    }
    m_windowDecorations.erase(it);
    // The audio-reactive decoration set may have shrunk — re-evaluate whether the
    // effect's cava instance is still needed. DEFERRED + coalesced: this runs as
    // the remove-first step of updateWindowDecoration on every focus/snap/rule
    // refresh, and a synchronous stop() here would block the compositor thread
    // (cava terminate + waitForFinished) and then respawn when the decoration is
    // re-added a step later. scheduleEffectAudioSync collapses the remove+readd
    // to one net decision at event-loop return. Runs before the transition
    // early-return below so both exits keep the run gate in sync.
    scheduleEffectAudioSync();
    // NB: m_focusFade is deliberately NOT scrubbed here. updateWindowDecoration
    // calls removeWindowDecoration as its remove-first step on every refresh
    // (focus change / snap flip / rule edit), so scrubbing here would reset the
    // focus cross-fade on each one. The undecorated→decorated snap is handled in
    // updateWindowDecoration (wasDecorated); close/delete scrub it explicitly.

    // Free the window's composite / buffer FBO targets, reclaiming GPU
    // memory — UNLESS a shader transition is mid-flight on this window. The
    // rotate/snap paths re-resolve the decoration DURING the animation
    // (state flip → rule flush → updateWindowDecoration → here), and erasing the
    // entry destroys the very composite the animation is sampling — the
    // at-draw probes caught windows animating with compositeTexId 0. The
    // transition keeps the state; its own teardown (endShaderTransition →
    // removeWindowDecoration, by then with no live transition) or the
    // windowDeleted backstop erases it.
    // Reuses the exact `target` resolved at the top (exact-id live match, else
    // the frozen reverse mapping, else the hint) — a fuzzy sibling here would
    // early-return on the SIBLING's transition and leak this window's FBO.
    // A decoration REFRESH (updateWindowDecoration's remove-first step) keeps the
    // GL working set. The targets are keyed on (size, chain, scale) and the fold
    // re-validates all three, so freeing them here only for the very next fold to
    // reallocate them is pure churn — and updateAllDecorations funnels through here
    // on every focus change, so it would free and reallocate every decorated
    // window's textures and framebuffers every time the user clicked another window.
    // Genuine teardown still frees.
    if (!keepSurfaceState) {
        releaseSurfaceState(windowId, target);
    }
}

// Free a window's composite / capture / prefix / buffer GL targets — UNLESS a
// shader transition is mid-flight on it.
//
// That guard is the whole reason this is a function. The rotate/snap paths
// re-resolve the decoration DURING an animation (state flip → rule flush →
// updateWindowDecoration), and erasing the state destroys the very composite the
// animation is still sampling: the at-draw probes caught windows animating with
// compositeTexId 0, and decoration_render.cpp's last-moment layer rebind and
// paint_pipeline.cpp's morph old-snapshot seed both reach for it without needing a
// decoration entry to exist. The transition keeps the state; its own teardown (by
// then with no live transition) or the windowDeleted backstop frees it.
//
// Every erase site must go through here. `target` is the exact window (an
// exact-id live match, else the frozen reverse mapping, else the caller's hint) —
// a fuzzy same-app sibling would early-return on the SIBLING's transition and leak
// this window's GL state.
void PlasmaZonesEffect::releaseSurfaceState(const QString& windowId, KWin::EffectWindow* target)
{
    if (target && m_shaderManager.findTransition(target)) {
        return;
    }
    // Erasing the entry destroys its GLTextures and GLFramebuffers, i.e. glDeleteTextures
    // and glDeleteFramebuffers. Most callers reach here off the paint cycle (a window
    // closing, a rule sweep on a zero-timer, a D-Bus reply), where the context is not
    // current.
    ensureGlContextCurrent();
    m_surfaceMultipass.erase(windowId);
}

// Hand the window's OffscreenEffect redirect and shader slot back to KWin, and
// damage what the decoration was covering.
//
// Split out of removeWindowDecoration because the REFRESH path deliberately skips it
// (the refresh re-asserts the same redirect a moment later, so tearing it down just
// makes KWin free and reallocate its OffscreenData) — but the paths where a refresh
// discovers the window is no longer decoratable must still run it, or the window
// would be left redirected and shaded with no decoration behind it.
void PlasmaZonesEffect::releaseDecorationGl(KWin::EffectWindow* w, int outerPadding)
{
    // setShader(nullptr) + unredirect makes KWin destroy the window's OffscreenData — its
    // texture and its framebuffer. Same discipline as releaseSurfaceState: most callers
    // reach here off the paint cycle.
    ensureGlContextCurrent();
    if (!w || m_shaderManager.findTransition(w)) {
        // A transition owns the slot; its own teardown does the handover.
        return;
    }
    setShader(w, nullptr);
    unredirect(w);
    // Dropping the redirect/shader on a STATIC window generates no damage of its own,
    // so without this the stale decorated frame lingers until something unrelated
    // repaints it.
    w->addRepaintFull();
    // A padded chain painted a margin band OUTSIDE the window rect; per-window
    // repaints clip to the window item, so damage the band at screen level or the
    // stale glow lingers after removal.
    if (outerPadding > 0 && KWin::effects) {
        QRectF padded = w->expandedGeometry();
        if (padded.isEmpty()) {
            padded = w->frameGeometry();
        }
        KWin::effects->addRepaint(
            KWin::RectF(padded.adjusted(-outerPadding, -outerPadding, outerPadding, outerPadding)));
    }
}

void PlasmaZonesEffect::clearAllDecorations()
{
    // Skip entries whose window is riding a live shader transition — the
    // close path DELIBERATELY keeps the closing window's border + multipass
    // entries alive so renderSurfaceChain can composite the decoration under
    // the close animation, and a close can still be in flight when this runs. It no
    // longer runs on focus shifts or autotile re-layouts (updateAllDecorations
    // reconciles in place now, and removes only what it did not revisit) — its two
    // remaining callers are the daemon-lost teardown and the effect's destructor. The
    // daemon dying does not wait for an animation to finish, so the guard stays.
    //
    // Removing them here nuked the kept state before the first close frame:
    // findWindowById refuses deleted windows and the bulk path has no window
    // hint, so removeWindowDecoration's own mid-transition guard could never
    // engage. The frozen reverse mapping (kept alive through the transition
    // by slotWindowClosed) resolves the deleted window here; the skipped
    // entry is erased by endShaderTransition's teardown or the windowDeleted
    // backstop.
    const QStringList ids = m_windowDecorations.keys();
    for (const QString& windowId : ids) {
        if (KWin::EffectWindow* w = m_windowIdReverse.value(windowId)) {
            if (m_shaderManager.findTransition(w)) {
                continue;
            }
        }
        removeWindowDecoration(windowId);
    }
}

} // namespace PlasmaZones
