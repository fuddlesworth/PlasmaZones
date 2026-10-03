// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// WindowTrackingAdaptor: the stale-window prune. The effect hands over the
// set of windows still alive, and every per-window store the adaptor and
// the engines keep drops the rest.

#include "windowtrackingadaptor.h"
#include "internal.h"
#include "core/platform/logging.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <QHash>
#include <QSet>

namespace PlasmaZones {

void WindowTrackingAdaptor::pruneStaleWindows(const QStringList& aliveWindowIds)
{
    // Fail CLOSED on an empty alive set, agreeing with both callees
    // (pruneStaleAssignments and pruneStaleInstances refuse it with a
    // warning): the effect's one-shot alive report at daemon-ready can fire
    // before session-restored apps have mapped, and wiping the shadow stores
    // (m_frameGeometry, m_broadcastFloating) on that empty report would
    // silence refreshOpenWindowPlacements and drop the close-path capture
    // fallback until the effect re-pushes.
    if (aliveWindowIds.isEmpty()) {
        qCWarning(lcDbusWindow) << "pruneStaleWindows: refusing empty alive set — nothing pruned";
        return;
    }
    const QSet<QString> alive(aliveWindowIds.begin(), aliveWindowIds.end());
    // Instance-keyed view of the same set, for the shadow maps keyed on
    // CANONICAL ids (the engine relays feed relayWindowFloatingChanged
    // canonical ids, so m_broadcastFloating must be swept in a key space that
    // survives a class rename — a raw sweep would erase a class-mutating
    // app's dedup entry every pass and the next relay would re-broadcast an
    // unchanged float state).
    QSet<QString> aliveInstances;
    aliveInstances.reserve(aliveWindowIds.size());
    // Canonical view of the same set, for the ENGINE prunes: the engines key
    // their internal maps on canonical ids (see the loop below).
    QSet<QString> canonicalAlive;
    canonicalAlive.reserve(aliveWindowIds.size());
    for (const QString& id : aliveWindowIds) {
        aliveInstances.insert(PhosphorIdentity::WindowId::extractInstanceId(id));
        canonicalAlive.insert(m_service->canonicalizeForLookup(id));
    }
    if (!m_lastActiveWindowId.isEmpty()
        && !aliveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(m_lastActiveWindowId))) {
        m_lastActiveWindowId.clear();
    }
    // Capture each dead window's final engine slot BEFORE ANY prune drops
    // state — pruneStaleAssignments wipes the SnapState the snap capture
    // answers from, and the engine prunes below untrack the tiling engines,
    // so this must run first or a silently-dead window's persisted record
    // stays as stale as the last save-timer sweep. Screen-less form: the
    // close-only branches (minimize preserve, orphan fallback, sibling
    // collapse) need an authoritative screen nobody has for a silently-dead
    // window. Keys are SNAPSHOTTED first — captureWindowPlacement fans out
    // into engine code, and mutating a QHash mid-iteration is undefined
    // (refreshOpenWindowPlacements documents the same discipline).
    const QStringList shadowIds = m_frameGeometry.keys();
    for (const QString& shadowId : shadowIds) {
        if (!aliveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(shadowId))) {
            captureWindowPlacement(shadowId);
            // The second close funnel, and it needs windowClosed's credit
            // revoke for the same reason it needs the capture above: this is
            // the backstop for a window that died with no close signal, and
            // without the revoke its record keeps a cross-screen reclaim
            // credit that homes every later same-app window on the dead
            // window's monitor for the rest of the session (#1017). AFTER the
            // capture, matching windowClosed's ordering. graceEligible=false:
            // the death happened at some unobserved earlier moment, so dating
            // it "now" would grant a shutdown grace it never earned.
            m_service->placementStore().markInstanceClosed(shadowId, /*graceEligible=*/false);
        }
    }
    int persistedPruned = m_service->pruneStaleAssignments(alive);
    if (m_autotileEngine || m_scrollEngine) {
        // The engines key every internal map (m_states reverse maps,
        // m_windowMinSizes, float markers, TilingState/strip membership)
        // on each window's CANONICAL id — its FIRST-seen composite, frozen by
        // the daemon-side WindowRegistry. The alive list, by contrast, carries
        // the effect's CURRENT composites: on an effect reload the effect
        // rebuilds its id cache from the windows' present WM_CLASS, so for an
        // app that mutated its class mid-session (Electron/CEF — the exact
        // class the canonicalization machinery exists for) the current
        // composite differs from the canonical. A raw comparison would then
        // miss the live window in the alive set and FORCE-REMOVE it from the
        // layout. Canonicalize each alive id back to its registry identity
        // ONCE (hoisted to the top of this method), then prune every
        // non-null engine with the same set (passthrough for ids the
        // registry never saw — never worse than the raw set).
        for (PhosphorEngine::PlacementEngineBase* engine : {m_autotileEngine.data(), m_scrollEngine.data()}) {
            if (engine) {
                persistedPruned += engine->pruneStaleWindows(canonicalAlive);
            }
        }
    }
    // Defensive sweep of the frame-geometry shadow store. The primary
    // cleanup path is `windowClosed`, but if a window dies without a
    // matching close signal reaching the adaptor (effect bug, compositor
    // crash, lost D-Bus call), the entry would otherwise leak forever.
    // The effect calls pruneStaleWindows precisely for this defensive
    // case — extend the same alive-set filter to m_frameGeometry. The map is
    // keyed on canonical ids, so it is swept in the instance-id key space: a
    // raw sweep would erase a class-mutating app's live entry every pass.
    int frameGeoPruned = 0;
    for (auto it = m_frameGeometry.begin(); it != m_frameGeometry.end();) {
        if (!aliveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(it.key()))) {
            // The evaluator's shared memo has no enumeration API, so it cannot
            // be swept by the alive-set predicate the local maps use. Evict by
            // the dead key instead: this map holds an entry for every window
            // the daemon tracked, so its dead keys are the dead windows.
            if (m_ruleEvaluator) {
                m_ruleEvaluator->evictCached(it.key());
            }
            it = m_frameGeometry.erase(it);
            ++frameGeoPruned;
        } else {
            ++it;
        }
    }
    // Fan the prune out to sibling adaptors' per-window caches (see the
    // signal doc). Consumers only erase from their OWN maps — nothing here
    // depends on emit-vs-sweep ordering. The payload is the same instance-id
    // view the local sweeps use, as a marshallable list: adaptor signals are
    // auto-relayed onto the bus, and QSet has no D-Bus signature.
    Q_EMIT stalePruned(QStringList(aliveInstances.cbegin(), aliveInstances.cend()));
    // Same defensive sweep for the last-broadcast floating shadow: an entry
    // would otherwise leak if the window died without a windowClosed signal.
    // Not persisted, so it does not feed the save-scheduling decision below.
    for (auto it = m_broadcastFloating.begin(); it != m_broadcastFloating.end();) {
        if (!aliveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(it.key()))) {
            it = m_broadcastFloating.erase(it);
        } else {
            ++it;
        }
    }
    // And the two open-path pending maps (#1106), same canonical key space:
    // a leak needs a window that died without a frame report or a close.
    const auto dead = [&aliveInstances](const auto& it) {
        return !aliveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(it.key()));
    };
    m_pendingOpenGeometry.removeIf(dead);
    m_pendingOpenSize.removeIf(dead);
    // And the tab-colour rule memo, for the same reason and in the same key
    // space — it is keyed on canonical ids too, so a raw sweep would erase a
    // class-mutating app's live entry every pass.
    for (auto it = m_tabColorMemo.begin(); it != m_tabColorMemo.end();) {
        if (!aliveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(it.key()))) {
            // Canonical-id twin of the evaluator eviction in the frame-geometry
            // sweep above: a resolve reached the shared memo under whichever id
            // its caller held, and evictCached is a no-op for a missing key.
            if (m_ruleEvaluator) {
                m_ruleEvaluator->evictCached(it.key());
            }
            it = m_tabColorMemo.erase(it);
        } else {
            ++it;
        }
    }
    // And the WindowRegistry's metadata records + canonical-id translations:
    // windowClosed releases these per-window, but a window that died without a
    // close signal (the case this whole method backstops) would leak its
    // record + canonical entry for the session. The registry keys on instance
    // ids (uuid components), so build the alive set in that form.
    if (m_windowRegistry) {
        m_windowRegistry->pruneStaleInstances(aliveInstances);
    }
    const int totalPruned = persistedPruned + frameGeoPruned;
    if (totalPruned > 0) {
        qCInfo(lcDbusWindow) << "Pruned" << persistedPruned << "stale persisted assignments and" << frameGeoPruned
                             << "shadow entries (not in KWin)";
    }
    // Only schedule a save when something PERSISTED was pruned. Frame-
    // geometry is the compositor-layer shadow store (not on disk), so a
    // frame-geo-only prune would fire scheduleSaveState → takeDirty →
    // DirtyNone-early-return — pointless debounced wake-up. Mirrors the
    // narrow-dirty-mask discipline the rest of the file follows.
    if (persistedPruned > 0) {
        scheduleSaveState();
    }
}

} // namespace PlasmaZones
