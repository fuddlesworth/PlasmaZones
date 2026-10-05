// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// WindowTrackingAdaptor: the stale-window prune. The effect hands over the
// set of windows still alive, and every per-window store the adaptor and
// the engines keep drops the rest.

#include "windowtrackingadaptor.h"
#include "lifecyclerelay.h"
#include "internal.h"
#include "core/platform/logging.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
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
        m_lastActiveScreenId.clear(); // as windowClosed does (F291)
    }
    // A screen report held for a dead window has nothing to replay into.
    m_heldScreenReports.removeIf([&aliveInstances](const auto& entry) {
        const qsizetype colon = entry.key().indexOf(QLatin1Char(':'));
        return colon > 0 && entry.key() != QLatin1String("active") && entry.key() != QLatin1String("cursor")
            && !aliveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(entry.key().mid(colon + 1)));
    });
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
        }
    }
    // The open claims of windows that died with no close signal go with them,
    // or each would hold the record it took at open away from every later
    // same-app open until eviction. Swept against the alive set rather than
    // the shadow keys above, so a claimer that never had a frame shadow is
    // reached too.
    m_service->placementStore().releaseOpenClaimsExcept(aliveInstances);
    // The reopen contract at daemon start: a live window whose OWN record
    // names another screen than the one its frame is on left that screen
    // while no daemon was watching, and stays where it is. Its engine slots
    // are released silently, once per daemon. The restore sweeps release the
    // windows they reach, but a minimized or excluded window reaches neither,
    // and a later layout change on the screen it left would resnap it there.
    // The effect seeds the frame shadow before this first prune, on the same
    // connection. Two records stay: one on an output that is absent now
    // (parked for its return), and one of a window that fills its output,
    // compared at output level, because that frame's centre lands in whichever
    // virtual screen holds the output's centre.
    if (!m_startupLeaveSweepDone) {
        m_startupLeaveSweepDone = true;
        PhosphorScreens::ScreenManager* screens = m_service->screenManager();
        for (auto it = m_frameGeometry.constBegin(); it != m_frameGeometry.constEnd(); ++it) {
            if (!it.value().isValid()
                || !aliveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(it.key()))) {
                continue;
            }
            const QString frameScreen = Utils::effectiveScreenIdAt(screens, it.value().center());
            const auto own = m_service->placementStore().peekExact(it.key());
            if (!own || !PhosphorEngine::ownRecordLeftScreen(*own, frameScreen)) {
                continue;
            }
            if (screens && !screens->physicalScreenFor(own->screenId).isValid()) {
                continue;
            }
            if (m_windowRegistry && m_windowRegistry->fillsOutputState(it.key()).value_or(false)
                && PhosphorIdentity::VirtualScreenId::samePhysical(own->screenId, frameScreen)) {
                continue;
            }
            for (auto slot = own->engines.constBegin(); slot != own->engines.constEnd(); ++slot) {
                m_service->releaseEngineSlot(it.key(), slot.key());
            }
            qCInfo(lcDbusWindow) << "pruneStaleWindows:" << it.key() << "left its recorded screen" << own->screenId
                                 << "for" << frameScreen << "while no daemon ran — its slots are released";
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
    m_lastManagedFrame.removeIf([&aliveInstances](const auto& entry) {
        return !aliveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(entry.key()));
    });
    int frameGeoPruned = 0;
    for (auto it = m_frameGeometry.begin(); it != m_frameGeometry.end();) {
        if (!aliveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(it.key()))) {
            // The evaluator's shared memo has no enumeration API, so it cannot
            // be swept by the alive-set predicate the local maps use. Evict by
            // the dead window's ids instead, both of them as windowClosed does:
            // this map's canonical key and the registry's current composite,
            // which differ for a window whose class changed (F466). The
            // registry still holds the dead instance here.
            if (m_ruleEvaluator) {
                m_ruleEvaluator->evictCached(it.key());
                const QString instance = PhosphorIdentity::WindowId::extractInstanceId(it.key());
                const QString raw = PhosphorIdentity::WindowId::buildCompositeId(
                    m_windowRegistry ? m_windowRegistry->appIdFor(instance) : QString(), instance);
                if (!raw.isEmpty() && raw != it.key()) {
                    m_ruleEvaluator->evictCached(raw);
                }
            }
            it = m_frameGeometry.erase(it);
            ++frameGeoPruned;
        } else {
            ++it;
        }
    }
    // Fan the prune out, in the same instance-id key space the local sweeps
    // use, to TilingAdaptor::pruneStaleFloatBroadcasts, which sweeps its
    // float-broadcast dedup, m_unclaimedOpens, m_pendingOpens and
    // m_moveReleasedInstances. It only erases from its own maps, so nothing
    // here depends on emit-vs-sweep ordering.
    Q_EMIT m_lifecycleRelay->stalePruned(QStringList(aliveInstances.cbegin(), aliveInstances.cend()));
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
