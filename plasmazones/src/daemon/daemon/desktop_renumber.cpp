// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Desktop and activity edits: KWin's desktop list renumbered (a desktop
// removed, inserted or moved, live or while the daemon was down), the desktop
// count changed, an activity went away. Every store keyed on a desktop NUMBER
// or an activity id is moved here (orchestrator_phaseb_1).

#include "daemon/daemon.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include <PhosphorEngine/DesktopRemap.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/RuleStore.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>

namespace PlasmaZones {

namespace {

/// The rules (layouts, modes, templates, gaps, the disable switches, which
/// are rules too, and the window rules that name a desktop) moved by @p remap
/// in one write: setAllRules persists once and emits rulesChanged once.
void applyContextRemap(PhosphorRules::RuleStore* store, const PhosphorRules::ContextRuleBridge::ContextRemap& remap)
{
    if (!store) {
        return;
    }
    if (const auto remapped = PhosphorRules::ContextRuleBridge::remapContextRules(store->ruleSet().rules(), remap)) {
        store->setAllRules(*remapped);
    }
}

} // namespace

void Daemon::onDesktopsRenumbered(const PhosphorEngine::DesktopRemap& remap)
{
    // The rules first, because everything below reads them: the re-seed
    // resolves each screen's mode, diffActiveAssignments resolves each context
    // through isContextDisabled, and hideDisabledAndRefresh is named for it.
    // Moving them afterwards would publish one pass built from the old
    // numbering (F786).
    applyContextRemap(m_ruleStore.get(),
                      {[&remap](int desktop) {
                           return remap.map(desktop);
                       },
                       {}});

    // Scrolling and the window registry take any change through the map
    // (ScrollEngine::remapDesktops, WindowRegistry::remapDesktops). Snap,
    // autotile and m_lastEngineOrders still take one removal at a time
    // (pruneStatesForDesktop + renumberDesktopsAfterRemoval): a change that
    // only removes desktops is applied as its removals in DESCENDING position
    // order, which keeps each later position valid: one removal live, several
    // at once, or what went while the daemon was down. An insert or a move has
    // no such decomposition; those two engines keep their numbering for it.
    QList<int> removedPositions;
    bool removalsOnly = true;
    int nextSurvivor = 1;
    for (int oldPosition = 1; oldPosition <= remap.oldCount(); ++oldPosition) {
        const int newPosition = remap.map(oldPosition);
        if (newPosition == 0) {
            removedPositions.prepend(oldPosition);
        } else if (newPosition == nextSurvivor) {
            ++nextSurvivor;
        } else {
            removalsOnly = false;
        }
    }
    // Same ordering the count handler documents: a live preview resolves its
    // state through a create-if-missing lookup, so a cancel arriving after the
    // prune would resurrect a state for the desktop that just went away. Ahead
    // of every engine change, the scroll remap's prunes included.
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->cancelDragInsertPreviews();
    }
    if (m_scrollEngine) {
        m_scrollEngine->remapDesktops(remap);
    }
    if (!removalsOnly) {
        qCWarning(lcDaemon) << "Virtual desktops were inserted or moved: the rules, scrolling and the window records"
                            << "follow their desktops, snap and autotile per-desktop state keeps its old numbering";
    } else if (!removedPositions.isEmpty()) {
        for (const int removedPosition : std::as_const(removedPositions)) {
            // Prune the removed position, THEN shift everything above it down
            // one. Both halves are needed and the order is not optional: the
            // prune leaves the position vacant so the first migration has
            // somewhere to land.
            //
            // Without the shift, every surviving higher state keeps the number
            // it had before while the compositor reports the new one, and the
            // desktop-membership reconcile then reads a window that never moved
            // as having left its desktop.
            for (PhosphorEngine::PlacementEngineBase* engine : {m_autotileEngine.get(), m_snapEngine.get()}) {
                if (!engine) {
                    continue;
                }
                engine->pruneStatesForDesktop(removedPosition);
                engine->renumberDesktopsAfterRemoval(removedPosition);
            }
            // The daemon's own desktop-keyed memo moves with them. It is the
            // window order a mode toggle re-seeds from, so leaving it on the
            // old numbering would hand the desktop that inherits a number the
            // order belonging to the one before it, and the deleted desktop's
            // own order would sit under the number its successor now uses.
            // Built fresh so an entry shifting down cannot land on one not yet
            // visited.
            QHash<TilingStateKey, QStringList> renumberedOrders;
            renumberedOrders.reserve(m_lastEngineOrders.size());
            for (auto it = m_lastEngineOrders.constBegin(); it != m_lastEngineOrders.constEnd(); ++it) {
                TilingStateKey key = it.key();
                if (key.desktop == removedPosition) {
                    continue;
                }
                if (key.desktop > removedPosition) {
                    --key.desktop;
                }
                renumberedOrders.insert(key, it.value());
            }
            m_lastEngineOrders = std::move(renumberedOrders);
        }
    }
    // The window records LAST. Each record the remap moves emits
    // metadataChanged, and its subscriber runs the desktop-membership
    // reconcile; with an engine still on the old numbering that reconcile
    // reads a window whose desktop was only renumbered as having left it, and
    // the drop-on-leave clears the zone or the column it still holds there.
    if (m_windowRegistry) {
        m_windowRegistry->remapDesktops(remap);
    }

    // At start-up (reconcileDesktopsAtStartup, m_running still false) the
    // first resolution comes after this anyway: start() runs
    // syncModeFromAssignments and finalizeStartup once the desktops are in.
    if (!m_running) {
        return;
    }
    // VirtualDesktopManager moved its own per-screen desktop map through the
    // same map, but SILENTLY: emitting screenDesktopChanged there would run
    // the whole context-switch pass against engine state that had not been
    // renumbered yet. Nothing else re-resolves, so the published active-layout
    // map and the overlays would keep the old answer for every screen whose
    // number moved. Every screen's number is already correct when this runs,
    // so the re-seed and the diff below resolve against the new numbering.
    // Not after an insert or a move: snap and autotile kept their old
    // numbering, and pushing them the new one would resolve each screen to
    // another desktop's state.
    if (removalsOnly) {
        reseedScreenDesktopsAfterRenumber();
        // A screen that showed a removed desktop now shows its successor,
        // whose assignment can be snapping: the recompute in the re-seed
        // demoted it, and nothing else on this path puts the released windows
        // back on their zones.
        flushPendingSnapZoneRestores();
    }
    diffActiveAssignments();
    if (m_overlayService) {
        m_overlayService->hideDisabledAndRefresh();
    }
    qCInfo(lcDaemon) << "Virtual desktops renumbered: removed" << removedPositions << "of" << remap.oldCount()
                     << "desktops; moved the rules, the window records and the states above them";
}

void Daemon::onDesktopCountChanged(int newCount)
{
    // Prune stale engine state and remembered orders for desktop numbers past
    // newCount.
    //
    // NOTE: KDE Plasma renumbers desktops when one in the middle is removed
    // (removing desktop 2 of 4 shifts 3→2 and 4→3), and this handler cannot
    // tell that happened: after a mid-list removal every surviving number is
    // still within newCount. onDesktopsRenumbered is where that case is
    // handled: it has the old-to-new map and re-keys the rules, the engines'
    // per-desktop state and m_lastEngineOrders. What is left for this handler
    // is the genuine out-of-range case.
    //
    // No rules here, deliberately. With KWin's list in use the count commits
    // with that list, AFTER desktopsRenumbered, so the map has already dropped
    // a removed desktop's rules, the last one included, and moved the rest.
    // The start-up sweep prunes them by count (reconcileDesktopsAtStartup).

    // A live preview must unwind BEFORE the prunes: autotile's cancel
    // resolves its state via a create-if-missing lookup, so a cancel arriving
    // AFTER the prune would resurrect a state for the deleted desktop (the two
    // context-switch handlers carry the same ordering).
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->cancelDragInsertPreviews();
    }

    // Desktop numbers are 1-based. Any state with desktop > newCount is
    // stale. desktopsWithActiveState() returns the desktops currently holding
    // state; filter for anything past the new count and prune, avoiding the
    // arbitrary upper bound of the old newCount+20 sweep. All THREE engines
    // carry their own stores, so prune each.
    for (PhosphorEngine::PlacementEngineBase* engine :
         {m_autotileEngine.get(), m_snapEngine.get(), m_scrollEngine.get()}) {
        if (!engine) {
            continue;
        }
        const QSet<int> active = engine->desktopsWithActiveState();
        for (int d : active) {
            if (d > newCount) {
                engine->pruneStatesForDesktop(d);
            }
        }
    }
    // Prune fallback assignment maps
    pruneContextMapsForDesktop(newCount);

    // No diffActiveAssignments() here, deliberately. The published
    // active-layout map is keyed by screen and resolved against each screen's
    // own current desktop, and every screen whose desktop number this removal
    // actually moved is announced through screenDesktopChanged instead:
    // VirtualDesktopManager clamps the out-of-range entries
    // (clampScreenDesktopsToCount) BEFORE it emits desktopCountChanged, so
    // their per-screen handler has already re-diffed by the time we run. A
    // mid-list removal that renumbers a still-in-range screen (desktop 3
    // becomes 2) is moved by VirtualDesktopManager itself through the map,
    // silently, and onDesktopsRenumbered does the re-diff the silence would
    // otherwise cost. pruneContextMapsForDesktop touches only
    // m_lastEngineOrders, which no resolution reads.
}

void Daemon::onActivitiesChanged()
{
    if (!m_activityManager) {
        return;
    }
    const QStringList activities = m_activityManager->activities();
    // Empty is the activity service going away: KDE always has one (F423).
    if (activities.isEmpty()) {
        return;
    }
    const QSet<QString> validSet(activities.begin(), activities.end());

    // The rules pinned to a removed activity go with it: its layouts, modes,
    // templates, gaps and disable switches (F801). Window rules that name an
    // activity are left as written.
    applyContextRemap(m_ruleStore.get(), {{}, [&validSet](const QString& activity) {
                                              return validSet.contains(activity);
                                          }});

    // Same cancel-before-prune ordering as desktopCountChanged: an
    // after-the-fact cancel would re-create state for a removed activity
    // through autotile's create-if-missing lookup.
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->cancelDragInsertPreviews();
    }

    // All three engines carry their own per-(screen,desktop,activity) stores,
    // so prune removed activities from each.
    for (PhosphorEngine::PlacementEngineBase* engine :
         {m_autotileEngine.get(), m_snapEngine.get(), m_scrollEngine.get()}) {
        if (engine) {
            engine->pruneStatesForActivities(activities);
        }
    }
    pruneContextMapsForActivities(validSet);
}

void Daemon::reconcileDesktopsAtStartup()
{
    // The desktop manager's first snapshot went out from init(), before any
    // handler above was connected, so nothing ran the desktop and activity
    // sweeps for what changed while the daemon was down (F809).
    //
    // Desktops removed, added or moved meanwhile: the desktop manager diffs
    // the list the last session saw against the live one and sends the map
    // through desktopsRenumbered, like a live change. At once when the first
    // snapshot is in, else against the first one that lands. An empty list
    // (the first start that keeps one) does nothing.
    if (m_windowTrackingAdaptor) {
        m_virtualDesktopManager->setBaselineDesktopIds(m_windowTrackingAdaptor->knownDesktopIds());
    }
    // The count sweep only once the snapshot is in: a failed first read
    // leaves no ids and a fallback count of 1, and sweeping against that
    // would delete every rule for desktop 2 and up.
    const QStringList liveIds = m_virtualDesktopManager->desktopIds();
    if (!liveIds.isEmpty()) {
        // A desktop past the count with no list to say which one went. Here,
        // unlike the live count handler, the count and the list came together.
        const int count = m_virtualDesktopManager->desktopCount();
        applyContextRemap(m_ruleStore.get(),
                          {[count](int desktop) {
                               return desktop > count ? 0 : desktop;
                           },
                           {}});
        onDesktopCountChanged(count);
        // Remembered for the next start. The first snapshot's desktopsChanged
        // went out before connectDesktopActivity's connection that keeps it
        // current from here on.
        if (m_windowTrackingAdaptor) {
            m_windowTrackingAdaptor->setKnownDesktopIds(liveIds);
        }
    }
    if (PhosphorWorkspaces::ActivityManager::isAvailable()) {
        onActivitiesChanged();
    }
}

void Daemon::pruneContextMapsForDesktop(int maxDesktop)
{
    auto it = m_lastEngineOrders.begin();
    while (it != m_lastEngineOrders.end()) {
        if (it.key().desktop > maxDesktop) {
            it = m_lastEngineOrders.erase(it);
        } else {
            ++it;
        }
    }
}

void Daemon::pruneContextMapsForActivities(const QSet<QString>& validActivities)
{
    auto it = m_lastEngineOrders.begin();
    while (it != m_lastEngineOrders.end()) {
        if (!it.key().activity.isEmpty() && !validActivities.contains(it.key().activity)) {
            it = m_lastEngineOrders.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace PlasmaZones
