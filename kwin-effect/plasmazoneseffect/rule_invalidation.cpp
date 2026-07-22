// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// RULE-CACHE INVALIDATION, split out of daemon_apply.cpp.
//
// A rule verdict is cached per (window, rule-set revision), so anything that changes
// what a rule would MATCH — a window's placement, its zone, the rule set itself — has
// to drop the cached verdicts it invalidates and re-resolve what the user sees. That is
// this file: the per-window invalidation, the coalescing flush behind it, the border
// sweep it schedules, and the whole-world invalidation used when the ground moves.

#include "plasmazoneseffect.h"

#include <effect/effecthandler.h>

#include <QSet>
#include <QString>
#include <QTimer>

#include <utility> // std::exchange, in flushPendingRuleInvalidations

namespace PlasmaZones {

void PlasmaZonesEffect::invalidateRuleCacheForStateChange(const QString& windowId)
{
    // Run when there are rules OR a config-default border / hidden title bar OR any
    // decoration-tree chain could apply: all are placement-sensitive (rule scopes,
    // appearance scope gating, and the placement-derived surface paths the tree
    // resolves through), so a snap / unsnap / zone change must re-resolve the
    // window's appearance. With none of them, a placement change can't change any
    // window's appearance — skip.
    if (m_shaderManager.animationRuleSet().isEmpty() && !hasWindowAppearanceDefault() && !hasDecorationTreeContent()) {
        return;
    }
    // Coalesce: a single float toggle emits BOTH windowFloatingChanged and
    // windowStateChanged, so this runs twice per logical change. Accumulate the
    // affected windowIds and flush once at the end of the event-loop turn — the
    // match-cache clear is global (running it per call is wasteful) and the
    // per-window border rebuild is otherwise repeated. The flush before the next
    // paint keeps the re-resolved border / opacity visually immediate.
    //
    // Only the CACHED verdicts (border / opacity) need invalidation. shouldHandleWindow's
    // exclusion query is evaluated on-demand every time it is consulted (drag start,
    // lifecycle filtering), so a snap/float/zone change is picked up at the next natural
    // call without eager re-filtering.
    const bool wasEmpty = m_pendingRuleInvalidations.isEmpty();
    m_pendingRuleInvalidations.insert(windowId);
    if (wasEmpty) {
        // `this` as the context object cancels the callback if the effect is torn
        // down before the turn ends.
        QTimer::singleShot(0, this, [this] {
            flushPendingRuleInvalidations();
        });
    }
}

void PlasmaZonesEffect::flushPendingRuleInvalidations()
{
    const QSet<QString> windowIds = std::exchange(m_pendingRuleInvalidations, {});
    if (windowIds.isEmpty()
        || (m_shaderManager.animationRuleSet().isEmpty() && !hasWindowAppearanceDefault()
            && !hasDecorationTreeContent())) {
        return;
    }
    // The match cache is keyed on (windowId, ruleSet revision); neither moves on a
    // placement-state change, so drop it once so border / opacity rules re-resolve
    // against the new snapped / floating / zone state.
    m_shaderManager.animationRuleEvaluator().clearCache();
    for (const QString& windowId : windowIds) {
        KWin::EffectWindow* w = findWindowById(windowId);
        if (!w) {
            continue;
        }
        // findWindowById's unambiguous-appId fallback can resolve a window whose
        // live id differs from the daemon-supplied one (stale uuid after a
        // cross-session restore). Key ALL downstream tracking by the LIVE id —
        // matching slotApplyGeometryRequested — or the layer reconcile below
        // would insert its pre-rule snapshot under a key no teardown path ever
        // removes and no live-id lookup (windowOwnKeepAbove / applyOwnLayerFlags
        // / the next reconcile) ever finds.
        const QString liveId = getWindowId(w);
        // Recreate this window's border so a state-scoped border colour
        // re-applies. Border overlays are visual-only, so build them only for a
        // window on the current desktop — matching updateAllDecorations, which gates
        // the same call this way to avoid building an invisible off-desktop item
        // that the next desktop switch tears down.
        if (w->isOnCurrentDesktop()) {
            updateWindowDecoration(liveId, w);
        }
        // Re-resolve the hide-title-bar override too: a SetHideTitleBar rule
        // scoped on a placement field (IsSnapped / IsFloating / Zone / Mode) must
        // recompute on a snap/unsnap, otherwise a title bar hidden WHEN isSnapped
        // stays hidden after unsnap until the next updateAllDecorations. The
        // decoration state survives desktop switches, so reconcile it for the
        // window regardless of which desktop it sits on (as updateAllDecorations does).
        reconcileRuleHiddenTitleBar(liveId, w);
        // Same for the stacking layer: a SetWindowLayer rule scoped on a
        // placement field (IsFloating / IsTiled / IsSnapped / Zone / Mode) must
        // re-apply on a float/snap flip — this is the trigger that makes
        // "floating windows above tiled windows" follow the float toggle.
        reconcileRuleWindowLayer(liveId, w);
        // No explicit repaint is needed for opacity: it is layer-backed now, so a
        // re-resolved SetOpacity value reaches the screen through the updateWindowDecoration
        // re-fold above, which repaints the window itself (an undecorated window carries no
        // rule opacity at all).
    }
}

void PlasmaZonesEffect::scheduleBorderSweep()
{
    if (m_borderSweepPending) {
        return;
    }
    m_borderSweepPending = true;
    // `this` as the context object cancels the callback if the effect is torn down
    // before the turn ends.
    QTimer::singleShot(0, this, [this] {
        m_borderSweepPending = false;
        updateAllDecorations();
    });
}

void PlasmaZonesEffect::invalidateAllRuleCaches()
{
    if (m_shaderManager.animationRuleSet().isEmpty() && m_ruleWindowLayerSnapshots.isEmpty()) {
        return;
    }
    // A bulk placement change (daemon loss clears the zone/floating caches; the
    // daemon-ready re-seeds repopulate them) moves neither the windowId nor the
    // ruleSet revision the match cache is keyed on, so every placement-scoped
    // verdict would survive stale. Drop the whole cache. The clear alone revives
    // nothing: appearance slots (opacity, tint, border colour) are FOLDED into the
    // window's decoration at updateWindowDecoration time, so each caller pairs this
    // sweep with its own decoration path — daemon loss tears the decorations down
    // (clearAllDecorations), and the daemon-ready re-seeds schedule a border sweep to
    // re-fold every window against the fresh placement.
    m_shaderManager.animationRuleEvaluator().clearCache();
    // Window-layer rules need the same placement-scoped re-resolve, but they
    // are EVENT-driven (during normal operation only reconcileRuleWindowLayer
    // writes keepAbove/keepBelow; restoreAllRuleWindowLayers is
    // destructor-only), so the cache clear above does nothing for them on its
    // own. Appearance (opacity / tint / border) re-folds through each caller's
    // decoration path (clearAllDecorations on daemon loss, scheduleBorderSweep
    // on re-seed); the layer does not, so it is re-reconciled here.
    // Re-reconcile every window right here at the bulk-placement chokepoint so
    // BOTH edges are covered: on daemon loss a `WHEN IsFloating` layer rule
    // releases its keep-above against the cleared placement (snapshot restore)
    // instead of stranding it for the daemon-down interval, and on the
    // daemon-ready re-seeds (getFloatingWindows / syncZonesFromDaemon replies)
    // it re-applies against the fresh placement even when the getAllRules
    // refresh fails (loadRuleAnimationsFromDbus early-returns on a D-Bus or
    // parse error without reconciling anything). NOT
    // restoreAllRuleWindowLayers(): that unconditionally drops all snapshots,
    // which would strand windows held by an unconditional (placement-free)
    // layer rule — the rule sets deliberately survive daemon loss, so those
    // must keep their applied layer. Gated on hasWindowLayerRules so a
    // session whose rules never touch the layer skips the cache-cold
    // per-window resolution (a lingering snapshot still sweeps to drain).
    if (KWin::effects && (m_shaderManager.hasWindowLayerRules() || !m_ruleWindowLayerSnapshots.isEmpty())) {
        const auto layerWindows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* lw : layerWindows) {
            if (lw && !lw->isDeleted()) {
                reconcileRuleWindowLayer(getWindowId(lw), lw);
            }
        }
    }
}

} // namespace PlasmaZones
