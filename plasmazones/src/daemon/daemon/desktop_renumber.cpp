// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Desktop and activity edits: KWin's desktop list renumbered (a desktop
// removed, inserted or moved, live or while the daemon was down), the desktop
// count changed, an activity went away. Every store keyed on a desktop NUMBER
// or an activity id is moved here (orchestrator_phaseb_1).

#include "daemon/daemon.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include "dbus/tilingadaptor/tilingadaptor.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include <PhosphorEngine/DesktopRemap.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/RuleStore.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>

#include <utility>

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
    // The start-up renumber, brought by a first desktop list that came late
    // (reconcileDesktopsAtStartup). The effect's window batch can beat it, and
    // whatever the engines placed from that batch is in the live numbering
    // already, so this corrects only what was restored from disk and not seen
    // live since (cr1_f1045_late_desktop_list): the rules start-up found and
    // nobody has edited since, the placement records no window has touched,
    // and the strips the scroll engine staged from the last session. The snap
    // and autotile engines restore nothing of their own (a reopen re-commits
    // their state from the records). Every engine's live state, the window
    // records, the order memo and a rule added or changed since keep their
    // numbers, and the rule change's own deferred reconcile re-resolves each
    // screen against the moved rules.
    if (const std::optional<quint64> restoredUpTo = std::exchange(m_lateStartupRenumberUpTo, std::nullopt)) {
        QHash<QUuid, PhosphorRules::Rule> startupRules;
        for (PhosphorRules::Rule& rule : std::exchange(m_lateStartupRules, {})) {
            const QUuid id = rule.id;
            startupRules.insert(id, std::move(rule));
        }
        applyContextRemap(m_ruleStore.get(),
                          {[&remap](int desktop) {
                               return remap.map(desktop);
                           },
                           {},
                           [&startupRules](const PhosphorRules::Rule& rule) {
                               const auto it = startupRules.constFind(rule.id);
                               return it != startupRules.constEnd() && *it == rule;
                           }});
        int recordsMoved = 0;
        if (PhosphorPlacement::WindowTrackingService* service =
                m_windowTrackingAdaptor ? m_windowTrackingAdaptor->service() : nullptr) {
            recordsMoved = service->placementStore().remapRestoredDesktopZones(remap, *restoredUpTo);
            if (recordsMoved > 0) {
                service->markDirty(PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements);
            }
        }
        if (auto* scroll = qobject_cast<PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get())) {
            scroll->remapRestoredStripStash(remap);
        }
        qCInfo(lcDaemon) << "Virtual desktops renumbered while the daemon was down, list read late:" << remap.oldCount()
                         << "->" << remap.newCount() << "desktops; moved the rules," << recordsMoved
                         << "restored placement record(s) and the restored strips";
        return;
    }

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

    // Same ordering the count handler documents: a live preview resolves its
    // state through a create-if-missing lookup, so a cancel arriving after the
    // prune would resurrect a state for the desktop that just went away. Ahead
    // of every engine change, the remaps' prunes included.
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->cancelDragInsertPreviews();
    }
    // KWin has already moved a removed desktop's windows to its successor, and
    // the effect may have re-announced them there, but every engine still
    // keys them under the removed number, so the remaps' prunes below release
    // them with the context. Noted before the first prune and handed back once
    // everything is renumbered (F800).
    QSet<int> removedDesktops;
    for (int oldDesktop = 1; oldDesktop <= remap.oldCount(); ++oldDesktop) {
        if (remap.map(oldDesktop) == 0) {
            removedDesktops.insert(oldDesktop);
        }
    }
    const bool readoptEvictees = m_running && !removedDesktops.isEmpty() && m_tilingAdaptor && m_windowTrackingAdaptor;
    if (readoptEvictees) {
        m_tilingAdaptor->noteRenumberEvictees(m_windowTrackingAdaptor->knownWindowIds(), removedDesktops);
    }
    // Every engine takes the change through the one map, an insert and a
    // reorder included: each prunes a removed desktop's state while the
    // numbers are still the old ones, then moves the rest with their desktops
    // (IPlacementEngine::remapDesktops). Snap's also moves every engine's
    // per-desktop zones in the placement records.
    for (PhosphorEngine::PlacementEngineBase* engine :
         {m_autotileEngine.get(), m_snapEngine.get(), m_scrollEngine.get()}) {
        if (engine) {
            engine->remapDesktops(remap);
        }
    }
    // The daemon's own desktop-keyed memo moves with them. It is the window
    // order a mode toggle re-seeds from, so leaving it on the old numbering
    // would hand a desktop the order of the one that had its number. Built
    // fresh so a moving entry cannot land on one not yet visited; a removed
    // desktop's order goes.
    QHash<TilingStateKey, QStringList> remappedOrders;
    remappedOrders.reserve(m_lastEngineOrders.size());
    for (auto it = m_lastEngineOrders.constBegin(); it != m_lastEngineOrders.constEnd(); ++it) {
        TilingStateKey key = it.key();
        const int to = remap.map(key.desktop);
        if (to == 0 && key.desktop != 0) {
            continue;
        }
        key.desktop = to;
        remappedOrders.insert(key, it.value());
    }
    m_lastEngineOrders = std::move(remappedOrders);
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
    // for any change, so the re-seed and the diff below resolve against the
    // new numbering.
    reseedScreenDesktopsAfterRenumber();
    // A screen that showed a removed desktop now shows its successor, whose
    // assignment can be snapping: the recompute in the re-seed demoted it, and
    // nothing else on this path puts the released windows back on their zones.
    flushPendingSnapZoneRestores();
    // After the registry's remap and the re-seed, so a window's span and its
    // screen's desktop are both in the new numbering: a window in view goes
    // back to the engine now, one out of view waits for the switch to it.
    if (readoptEvictees) {
        m_tilingAdaptor->readoptRenumberEvictees(
            [this](const QString& screenId, const PhosphorEngine::DesktopSpan& span) {
                return span.coversDesktop(m_virtualDesktopManager->currentDesktopForScreen(screenId))
                    && span.coversActivity(
                        PhosphorWorkspaces::ActivityManager::currentActivityOrEmpty(m_activityManager.get()));
            });
    }
    diffActiveAssignments();
    if (m_overlayService) {
        m_overlayService->hideDisabledAndRefresh();
    }
    qCInfo(lcDaemon) << "Virtual desktops renumbered:" << remap.oldCount() << "->" << remap.newCount()
                     << "desktops; moved the rules, every engine's per-desktop state and the window records";
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
    // The rules naming a desktop past the count go too. With KWin's list in
    // use a shrinking count commits with that list, AFTER desktopsRenumbered,
    // so the map has already dropped them and this finds nothing to write; a
    // growing count goes out first (F1022) and finds nothing past it. Without
    // the list (onNumberOfDesktopsChanged's fallback commits a bare count),
    // and at start-up (reconcileDesktopsAtStartup), this is the only prune
    // they get (F1044).
    applyContextRemap(m_ruleStore.get(),
                      {[newCount](int desktop) {
                           return desktop > newCount ? 0 : desktop;
                       },
                       {}});

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
    //
    // A failed first read leaves no list until a later refresh, and the
    // effect's window batch can arrive first: windows are then placed under
    // the live numbering before the map exists. The renumber that list brings
    // moves only what was restored and not seen live since (F1045), so the
    // stamp of the newest restored placement record and the rules as they
    // stand are taken here, before any window opens or rule is written. The
    // desktopsChanged handler drops both when the late list renumbers nothing.
    const QStringList liveIds = m_virtualDesktopManager->desktopIds();
    m_lateStartupRenumberUpTo.reset();
    m_lateStartupRules.clear();
    if (m_windowTrackingAdaptor) {
        const QStringList knownIds = m_windowTrackingAdaptor->knownDesktopIds();
        if (liveIds.isEmpty() && !knownIds.isEmpty()) {
            const PhosphorPlacement::WindowTrackingService* service = m_windowTrackingAdaptor->service();
            m_lateStartupRenumberUpTo = service ? service->placementStore().sequence() : 0;
            m_lateStartupRules = m_ruleStore ? m_ruleStore->ruleSet().rules() : QList<PhosphorRules::Rule>();
        }
        m_virtualDesktopManager->setBaselineDesktopIds(knownIds);
    }
    // The count sweep only once the snapshot is in: a failed first read
    // leaves no ids and a fallback count of 1, and sweeping against that
    // would delete every rule for desktop 2 and up.
    if (!liveIds.isEmpty()) {
        // A desktop past the count with no list to say which one went: the
        // count handler drops its rules with its engine state.
        onDesktopCountChanged(m_virtualDesktopManager->desktopCount());
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
