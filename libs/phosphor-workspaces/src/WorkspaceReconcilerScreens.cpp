// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The screen-lifecycle half of WorkspaceReconciler: hotplug (add, remove,
// reorder, and the home-stamp migration that carries an output's workspaces
// back when it returns) and first-run/restart adoption. Split out of
// WorkspaceReconciler.cpp on file size, the same way WorkspaceReconcilerNamed
// .cpp is; the state and every helper used here are declared in the one
// header, and the logging category is defined there.

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QLoggingCategory>
#include <QSet>

Q_DECLARE_LOGGING_CATEGORY(lcWorkspaceRec)

namespace PhosphorWorkspaces {

// ── Screens ─────────────────────────────────────────────────────────────────

void WorkspaceReconciler::onScreenAdded(const QString& screenId)
{
    // hasScreen, not knowsScreen: the two real signal orders a hotplug arrives
    // in disagree about which of these runs first. When the controller's
    // recomputed screen order lands BEFORE the screenAdded (the order the
    // daemon actually produces on some hotplugs), the new output is already in
    // the screen order by the time we get here, so knowsScreen answered true
    // and the screen was returned from — never migrated home, never given a
    // desktop, silently left holding nothing. Holding a SLICE is the real
    // "already dealt with" test, and setScreenOrder de-duplicates, so appending
    // an id it already carries is a no-op.
    if (screenId.isEmpty() || m_map.hasScreen(screenId)) {
        return;
    }
    m_map.setScreenOrder(m_map.screenOrder() << screenId);

    // Migrate displaced workspaces home (hotplug memory): every entry whose
    // homeScreenId names the returning output moves back, in its current
    // relative order, ahead of whatever trailing empty maintenance adds.
    QStringList returning;
    const QStringList owned = m_map.allDesktopIds();
    for (const QString& id : owned) {
        if (m_map.entryFor(id).homeScreenId == screenId) {
            returning.append(id);
        }
    }
    QSet<QString> touchedScreens;
    int index = 0;
    for (const QString& id : returning) {
        touchedScreens.insert(m_map.ownerOf(id));
        m_map.transfer(id, screenId, index++);
        m_map.setHomeScreen(id, QString());
    }

    maintainScreen(screenId);
    for (const QString& touched : touchedScreens) {
        maintainScreen(touched); // the foster screen's invariants re-settle
        // The foster screen may have been SHOWING one of the migrated
        // desktops; it now sits on a foreign one and needs the owner-wins
        // correction, exactly as the verb-side transfer path does.
        evaluateForeign(touched);
    }
    bumpGeneration();
}

void WorkspaceReconciler::onScreenRemoved(const QString& screenId)
{
    if (!m_map.knowsScreen(screenId)) {
        return;
    }
    const QList<WorkspaceEntry> orphaned = m_map.takeSlice(screenId);
    m_currentByScreen.remove(screenId);
    // The snap-back memo names a desktop this screen no longer owns; keeping
    // it would aim a later correction (after a replug re-adds the screen) at
    // whatever that desktop became in the meantime.
    m_lastOwnedByScreen.remove(screenId);
    // Screen-scoped ledger entries die with the screen: an open SetCurrent for
    // a gone output can never be echoed, and until it expired it would block
    // the corrections of a re-added screen with the same id and then fire a
    // spurious resync.
    for (int i = m_ledger.size() - 1; i >= 0; --i) {
        if (m_ledger.at(i).screenId == screenId) {
            m_ledger.removeAt(i);
        }
    }
    if (m_ledger.isEmpty()) {
        m_ledgerTimer.stop();
    }
    if (m_focusedScreen == screenId) {
        m_focusedScreen.clear();
    }

    QString fallback;
    const QStringList order = m_map.screenOrder();
    if (!order.isEmpty()) {
        fallback = order.first();
    }
    if (fallback.isEmpty()) {
        // Last screen went away; keep nothing (KWin keeps the desktops; the
        // next screenAdded re-adopts via list repair).
        bumpGeneration();
        return;
    }
    // Append before the fallback's trailing empty, preserving order, each
    // entry stamped with its home output for migrate-back on replug. A
    // second displacement keeps the ORIGINAL home (the entry already carries
    // one), so daisy-chained unplugs still return the workspace to where the
    // user put it.
    int index = insertIndexBeforeTrailingEmpty(fallback);
    for (auto entry : orphaned) {
        if (entry.homeScreenId.isEmpty()) {
            entry.homeScreenId = screenId;
        }
        m_map.insert(fallback, index++, entry);
    }
    maintainScreen(fallback);
    bumpGeneration();
}

void WorkspaceReconciler::onScreenOrderChanged(const QStringList& order)
{
    const QStringList before = m_map.screenOrder();
    if (order == before) {
        return;
    }
    m_map.setScreenOrder(order);
    // setScreenOrder REPAIRS the order it is handed (slice-holding screens
    // missing from it are appended), so a caller-order that differs can still
    // repair back to what we already had. Compare the result, not the request.
    if (m_map.screenOrder() == before) {
        return;
    }
    bumpGeneration();
}

// ── Adoption ────────────────────────────────────────────────────────────────

void WorkspaceReconciler::adoptAll(const QStringList& ids, const QHash<QString, QString>& currentDesktopIdByScreen)
{
    m_lastIds = ids;

    // Keep consistent restored content; repair drops vanished ids and returns
    // what still needs an owner.
    QStringList unowned = m_map.repairAgainst(ids);

    // Pass 1: each desktop currently shown by a screen goes to that screen;
    // ties resolve to the first screen in order (the map insert repairs
    // double-ownership by removal, so process in screen order and skip ids
    // already owned).
    const QStringList order = m_map.screenOrder();
    for (const QString& screenId : order) {
        const QString shownId = currentDesktopIdByScreen.value(screenId);
        if (shownId.isEmpty()) {
            continue;
        }
        // Seed the per-screen current from what the caller observed. Without
        // it every verb resolves empty until the effect's first per-output
        // report happens along, which can be a long time after adoption on a
        // session that starts on its restored desktops.
        const int globalIndex = ids.indexOf(shownId);
        if (globalIndex >= 0) {
            m_currentByScreen.insert(screenId, globalIndex + 1);
            if (m_map.ownerOf(shownId) == screenId) {
                m_lastOwnedByScreen.insert(screenId, shownId);
            }
        }
        if (unowned.contains(shownId)) {
            WorkspaceEntry entry;
            entry.desktopId = shownId;
            m_map.insert(screenId, insertIndexBeforeTrailingEmpty(screenId), entry);
            unowned.removeAll(shownId);
            m_lastOwnedByScreen.insert(screenId, shownId);
        }
    }

    // Pass 2: remaining desktops keep KWin order contiguous — each unowned id
    // joins the owner of the nearest preceding owned desktop in global order;
    // a leading run goes to the first screen.
    for (const QString& id : unowned) {
        const int globalIdx = ids.indexOf(id);
        QString target;
        for (int i = globalIdx - 1; i >= 0; --i) {
            target = m_map.ownerOf(ids.at(i));
            if (!target.isEmpty()) {
                break;
            }
        }
        if (target.isEmpty()) {
            target = order.isEmpty() ? QString() : order.first();
        }
        if (target.isEmpty()) {
            qCWarning(lcWorkspaceRec) << "adoption with no screens; desktop left unowned:" << id;
            continue;
        }
        WorkspaceEntry entry;
        entry.desktopId = id;
        m_map.insert(target, insertIndexBeforeTrailingEmpty(target), entry);
    }

    maintainInvariants();
    bumpGeneration();
}

} // namespace PhosphorWorkspaces
