// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Window-RULE reconcilers: the rule actions that change a window's own KWin state
// rather than its decoration chain — a hidden title bar (SetHideTitleBar) and the
// keep-above / keep-below stacking layer (SetWindowLayer, with the per-mode
// keep-floating-above config default filling the slot no rule owns).
//
// Split out of decorations.cpp, which owns the decoration chain (resolve, apply,
// tear down). These share that file's per-window rule-resolution entry points but
// are a separate concern: a window can carry either of these with no decoration at
// all, and a decorated window can carry neither.

#include "plasmazoneseffect.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <PhosphorRules/RuleAction.h>

#include "shader_resolve.h"
#include "tilinghandler/tilinghandler.h"
#include "window_query.h"

#include <optional>
#include <utility> // std::exchange

namespace PlasmaZones {

void PlasmaZonesEffect::reconcileRuleHiddenTitleBar(const QString& windowId, KWin::EffectWindow* w)
{
    // isDeleted matches the two sibling reconcilers below. Every current
    // caller pre-checks it, so this is defence in depth: the body reads the
    // window class and, on the non-shielded arm, walks the rule query — the
    // exact id-cache re-pollution shouldHandleWindow guards corpses out of.
    if (!w || w->isDeleted() || windowId.isEmpty()) {
        return;
    }
    // Tri-state override, forwarded to the DecorationManager (Rule is the only
    // owner kind now — there are no mode owners to defer to):
    //   unset → no owner, the title bar shows
    //   true  → hide the title bar (a rule, or the config default in scope)
    //   false → FORCE-SHOW (a veto pinning the decoration visible — only an
    //           explicit SetHideTitleBar=false rule; the config default never
    //           contributes a force-show, see resolveEffectiveWindowAppearance)
    // The manager owns the capability gate and the geometry re-assert across
    // veto-driven decoration flips.
    //
    // Windowed-fullscreen hold: skipped outright, mirroring the layer
    // reconciler below. The manager's veto path re-asserts geometry across a
    // decoration flip, and mid-hold that re-assert would fight the strip's
    // committed column rect (the hold's whole contract is that the rect
    // stays the slot). The un-flag paths all drive updateAllDecorations,
    // which re-runs this reconciler, so a rule change made during the hold
    // lands at un-flag time.
    if (m_windowedFullscreenWindows.contains(windowId)) {
        return;
    }
    // Structural shield, same predicate the layer and open-fullscreen
    // reconcilers use: hiding a title bar is a persistent window-state write,
    // so a broad match expression must not reach a dock, the desktop, a
    // notification, an OSD, or our own overlays. Previously unshielded, which
    // let a titleBarScope=All config default resolve a hide for plasmashell's
    // panel — inert only because KWin refuses to unset borders on a dock. A
    // shielded window resolves to a DISENGAGED override rather than
    // early-returning, so a window that mutated into a shielded class drains
    // the override it was holding instead of stranding a hidden title bar.
    const std::optional<bool> hideTitleBar =
        isRuleShieldedSurface(w) ? std::nullopt : resolveEffectiveWindowAppearance(w, windowId).hideTitleBar;
    m_decorationManager->setRuleOverride(windowId, hideTitleBar);
}

void PlasmaZonesEffect::reconcileRuleWindowLayer(const QString& windowId, KWin::EffectWindow* w)
{
    if (!w || w->isDeleted() || windowId.isEmpty()) {
        return;
    }
    // No-layer-rules fast path: with no enabled SetWindowLayer rule and no
    // snapshot to drain, the resolve below can neither apply nor restore
    // anything. Keeps the "no-rules case pays nothing" invariant (see
    // shouldAnimateWindow) on the focus-driven updateAllDecorations path —
    // without this, the layer reconcile would be the one consumer building
    // the ~30-accessor rule query for sessions whose rules never touch the
    // layer (opacity/border-only rule sets included). A lingering snapshot
    // still falls through so the restore below drains it.
    // The per-mode keep-floating-above config default is the layer slot's
    // config-side filler (rule slot ?? config default, the hideTitleBar shape),
    // so it opens the gate the same way a layer rule does.
    if (!m_shaderManager.hasWindowLayerRules() && !m_windowAppearanceDefault.anyKeepFloatingAbove()
        && !m_ruleWindowLayerSnapshots.contains(windowId)) {
        return;
    }
    // Windowed-fullscreen tiles own their keep flags for the duration of
    // the hold: the feature keeps keep-below applied to defeat KWin's
    // active-fullscreen layer promotion (TilingHandler::
    // applyWindowedFullscreenLayerDemotion), and a rule writing over it
    // would re-promote the tile above its strip. While flagged the window
    // is skipped OUTRIGHT — apply and restore both: draining a parked rule
    // snapshot here would clobber the demotion with the user's flags. The
    // un-flag paths restore the pre-demotion flags, and the reconcile-driving
    // ones (the batch un-flag arm's onComplete, the client self-exit arm, the
    // float cleanups, the mode-swap/desktop-demote sweeps) run
    // updateAllDecorations on the same edge, so those drain a parked
    // snapshot at un-flag time. The snap<->snap screen-leave release
    // (outputchange.cpp) is the one un-flag path that drives no reconcile of
    // its own — a snapshot parked there drains at the next natural
    // updateAllDecorations (any focus change), and restoreAllRuleWindowLayers
    // covers teardown, so the wait is bounded by the next sweep rather than
    // by the un-flag edge itself.
    if (m_windowedFullscreenWindows.contains(windowId)) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    // Structural / own-surface shield, extending the SetOpacity paint gate
    // (paint_pipeline.cpp shields only the overlay / plasmashell classes;
    // the portal and structural entries in the shared predicate are
    // deliberate hardening for a raw window-state write). See
    // isRuleShieldedSurface for the full rationale, including why transients
    // are deliberately absent from it. A shielded window resolves as
    // rule-free rather than early-returning — window classification can
    // mutate mid-session (the Electron/CEF class swap, an X11 type change),
    // and a window that was rule-held BEFORE mutating into a shielded class
    // must drain its snapshot through the restore branch below instead of
    // stranding the rule's flags. Fresh shielded windows skip the resolve
    // entirely and never enter the map.
    std::optional<QString> layer;
    if (!isRuleShieldedSurface(w)) {
        // Rule slot first (an explicit SetWindowLayer of any token, Normal and
        // Below included, beats the config default), then the per-mode
        // keep-floating-above default for a floated window. hasWindowLayerRules
        // gates the rule query so a default-only session never builds it.
        if (m_shaderManager.hasWindowLayerRules()) {
            layer = resolveWindowLayer(resolveRuleActions(w, windowId));
        }
        if (!layer && keepFloatingAboveDefault(windowId, w)) {
            layer = QString(PhosphorRules::WindowLayerToken::Above);
        }
    }
    const auto it = m_ruleWindowLayerSnapshots.find(windowId);
    if (!layer) {
        // No rule owns the layer. If one did before, put the user's own flags
        // back exactly once and forget the window. Erase BEFORE the setters:
        // they emit KWin signals, and holding a QHash iterator across code
        // that could re-enter this map would be undefined if a future
        // connection routes a keep-above change back into a reconcile.
        if (it != m_ruleWindowLayerSnapshots.end()) {
            const WindowLayerSnapshot snapshot = *it;
            m_ruleWindowLayerSnapshots.erase(it);
            kw->setKeepAbove(snapshot.keepAbove);
            kw->setKeepBelow(snapshot.keepBelow);
        }
        return;
    }
    // First application snapshots the pre-rule flags so the restore above
    // returns the window to the USER's state. Deliberately not re-captured
    // while a rule owns the layer: a manual keepAbove toggle under an active
    // rule is re-asserted away on the next reconcile (the rule owns the
    // property while it matches, same as the DecorationManager rule override),
    // and capturing it would corrupt the restore target.
    if (it == m_ruleWindowLayerSnapshots.end()) {
        m_ruleWindowLayerSnapshots.insert(windowId, {kw->keepAbove(), kw->keepBelow()});
    }
    // Always write the fully-specified pair. Writing only the token's "own"
    // flag leaves the opposite one stale on an above→below rule flip (the
    // Krohnkite asymmetry bug); KWin's setters are change-gated, so the
    // re-asserts are free in the steady state.
    const bool above = (*layer == PhosphorRules::WindowLayerToken::Above);
    const bool below = (*layer == PhosphorRules::WindowLayerToken::Below);
    kw->setKeepAbove(above);
    kw->setKeepBelow(below);
}

bool PlasmaZonesEffect::keepFloatingAboveDefault(const QString& windowId, KWin::EffectWindow* w) const
{
    const WindowAppearanceDefault& def = m_windowAppearanceDefault;
    if (!def.anyKeepFloatingAbove() || !isWindowFloating(windowId)) {
        return false;
    }
    // Only for a window on the CURRENT desktop. The per-screen mode sets
    // below are resolved against the current desktop (the daemon re-announces
    // them on every desktop switch), so they say nothing about the desktop an
    // off-desktop window actually lives on — and the reconcile sweep runs for
    // every window on every desktop. Without this gate, sitting on a tiled
    // desktop granted keep-above to every FloatingCache-floating window
    // parked on an UNASSIGNED desktop of the same screen, on every sweep, so
    // the bit stuck until effect teardown restored it (Discussion #1028,
    // "windows on non-assigned desktops got keep-above"). Answering false
    // here resolves the layer to null for that window, and the snapshot
    // restore in reconcileRuleWindowLayer drains the bit on the same sweep;
    // re-entering a desktop this mode manages re-grants it the same way.
    if (!w->isOnCurrentDesktop()) {
        return false;
    }
    // Screen mode from the tiling handler's per-screen sets: a scrolling
    // screen is a managed screen also in the scrolling set, any other
    // managed screen is autotile, and an unmanaged screen is snapping. Note
    // the FloatingCache is a mode-agnostic union and KEEPS a window's entry
    // when it moves to a context no engine manages (the stale-float
    // contract), which is why the desktop gate above must not rely on it.
    const QString screenId = getWindowScreenId(w, windowId);
    if (m_tilingHandler->isScrollingScreen(screenId)) {
        return def.keepFloatingAboveScrolling;
    }
    if (m_tilingHandler->isManagedScreen(screenId)) {
        return def.keepFloatingAboveTiling;
    }
    return def.keepFloatingAboveSnapping;
}

void PlasmaZonesEffect::applyRuleOpenFullscreen(const QString& windowId, KWin::EffectWindow* w)
{
    if (!w || w->isDeleted() || windowId.isEmpty()) {
        return;
    }
    // No-rules fast path, same shape as the layer reconcile above — no
    // snapshot map to drain here because the verdict is one-shot: it is
    // applied exactly once, at windowAdded, and never re-reconciled (a rule
    // edit mid-session must not yank an open window into or out of
    // fullscreen; that is niri's open-fullscreen contract too).
    if (!m_shaderManager.hasOpenFullscreenRules()) {
        return;
    }
    // Same structural shield as the layer reconcile: a broad match must
    // never fullscreen a dock, a notification, or our own overlay. Shielded
    // windows skip the resolve entirely — with no snapshot to drain there is
    // no restore branch to route them through, so the early return that would
    // be wrong for the other two reconcilers is right here.
    if (isRuleShieldedSurface(w)) {
        return;
    }
    // Through the VERDICT evaluator, not the animation/appearance one: that
    // evaluator honours ExcludeAnimations as a walk-stopper, so an
    // "exclude this app from animations" rule at a higher priority cancelled
    // the open-fullscreen decision before its slot could fill.
    const std::optional<bool> verdict = resolveOpenFullscreen(resolveRuleVerdictActions(w, windowId));
    if (!verdict) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    // Gate on the REQUESTED state (synchronous), not the committed one: at
    // windowAdded a client that mapped fullscreen already carries the
    // requested bit while the committed state may lag a round-trip. The flip
    // runs BEFORE the window is announced to the daemon (this is called from
    // slotWindowAdded ahead of the routing block), and isEligibleForTilingNotify
    // rejects on requested-OR-committed fullscreen, so the announce path sees
    // this window's final state either way.
    //
    // Bracketed through TilingHandler: on XWayland setFullScreen emits
    // windowFullScreenChanged SYNCHRONOUSLY, re-entering
    // slotWindowFullScreenChanged from inside slotWindowAdded — before this
    // window has been announced at all. Its never-tracked exit arm then runs
    // notifyWindowAdded for a window mid-open, releasing the first-frame
    // restore suppression slotWindowAdded is about to arm and producing a
    // visible spawn-then-jump. The counter is TilingHandler-private, so the
    // bracketed write lives there (applyFullScreenSuppressed) rather than
    // widening access to it.
    if (*verdict && !kw->isRequestedFullScreen()) {
        m_tilingHandler->applyFullScreenSuppressed(kw, true);
    } else if (!*verdict && kw->isRequestedFullScreen()) {
        m_tilingHandler->applyFullScreenSuppressed(kw, false);
    }
}

std::optional<qreal> PlasmaZonesEffect::ruleScrollFactorFor(KWin::EffectWindow* w) const
{
    if (!w || w->isDeleted() || !m_shaderManager.hasScrollFactorRules()) {
        return std::nullopt;
    }
    const QString windowId = getWindowId(w);
    if (windowId.isEmpty()) {
        return std::nullopt;
    }
    // Verdict evaluator, same reason as applyRuleOpenFullscreen above: a
    // scroll multiplier is not an animation, so ExcludeAnimations must not
    // stop the walk that fills its slot.
    return resolveScrollFactor(resolveRuleVerdictActions(w, windowId));
}

void PlasmaZonesEffect::restoreAllRuleWindowLayers()
{
    const QHash<QString, WindowLayerSnapshot> snapshots = std::exchange(m_ruleWindowLayerSnapshots, {});
    for (auto it = snapshots.cbegin(); it != snapshots.cend(); ++it) {
        KWin::EffectWindow* w = findWindowById(it.key());
        // Exact-id re-check, matching every other findWindowById consumer in
        // this file: the fuzzy appId fallback can resolve a same-app SIBLING
        // for a gone windowId, and restoring one window's snapshot onto
        // another would clobber the sibling's own flags.
        if (!w || w->isDeleted() || getWindowId(w) != it.key()) {
            continue;
        }
        if (KWin::Window* kw = w->window()) {
            kw->setKeepAbove(it->keepAbove);
            kw->setKeepBelow(it->keepBelow);
        }
    }
}

} // namespace PlasmaZones
