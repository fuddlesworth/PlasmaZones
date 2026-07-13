// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Window-RULE reconcilers: the rule actions that change a window's own KWin state
// rather than its decoration chain — a hidden title bar (SetHideTitleBar) and the
// keep-above / keep-below stacking layer (SetWindowLayer).
//
// Split out of decorations.cpp, which owns the decoration chain (resolve, apply,
// tear down), to keep that TU under the 800-line limit. These share the file's
// per-window rule-resolution entry points but are a separate concern: a window can
// carry either of these with no decoration at all, and a decorated window can carry
// neither.

#include "../plasmazoneseffect.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <PhosphorRules/RuleAction.h>

#include "shader_resolve.h"
#include "window_query.h"

#include <optional>
#include <utility> // std::exchange

namespace PlasmaZones {

void PlasmaZonesEffect::reconcileRuleHiddenTitleBar(const QString& windowId, KWin::EffectWindow* w)
{
    if (!w || windowId.isEmpty()) {
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
    const ResolvedWindowAppearance ovr = resolveEffectiveWindowAppearance(w, windowId);
    m_decorationManager->setRuleOverride(windowId, ovr.hideTitleBar);
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
    if (!m_shaderManager.hasWindowLayerRules() && !m_ruleWindowLayerSnapshots.contains(windowId)) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    // Structural / own-surface shield, extending the SetOpacity paint gate
    // (paint_pipeline.cpp shields only the overlay / plasmashell classes;
    // the portal and structural entries here are deliberate hardening for a
    // raw window-state write): a broad match expression must never demote a
    // dock, pin a notification, or strip the daemon overlay's own
    // keep-above. Transients / popups are deliberately NOT shielded:
    // transient utility surfaces are legitimate layer-rule targets, and
    // transient exclusion is per-feature user opt-in in this project (the
    // IsTransient match field), never hardcoded policy. A shielded window
    // resolves as rule-free rather than early-returning — window
    // classification can mutate mid-session (the Electron/CEF class swap,
    // an X11 type change), and a window that was rule-held BEFORE mutating
    // into a shielded class must drain its snapshot through the restore
    // branch below instead of stranding the rule's flags. Fresh shielded
    // windows skip the resolve entirely and never enter the map.
    const QString winClass = w->windowClass();
    const bool shielded = isOwnOverlayClass(winClass) || isPlasmaShellSurface(winClass)
        || isXdgDesktopPortalSurface(winClass) || w->isDesktop() || w->isDock() || w->isNotification()
        || w->isCriticalNotification() || w->isOnScreenDisplay();
    std::optional<QString> layer;
    if (!shielded) {
        layer = resolveWindowLayer(resolveRuleActions(w, windowId));
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
