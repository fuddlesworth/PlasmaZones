// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The by-id verbs the workspace overview drives: reorder and transfer a
// workspace named by desktop id, insert a reserved workspace for a drop,
// and push a dynamic workspace's name. Kept apart from the reconciler's
// lifecycle TU so that file stays under the size ceiling.

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QDateTime>

namespace PhosphorWorkspaces {

bool WorkspaceReconciler::reorderWorkspace(const QString& desktopId, int newSliceIndex)
{
    const QString owner = m_map.ownerOf(desktopId);
    if (owner.isEmpty()) {
        return false;
    }
    const int size = m_map.sliceSize(owner);
    if (newSliceIndex < 0 || newSliceIndex >= size) {
        return false;
    }
    if (m_map.sliceIndexOf(desktopId) == newSliceIndex) {
        return false;
    }
    if (!m_map.reorderWithinSlice(desktopId, newSliceIndex)) {
        return false;
    }
    maintainScreen(owner); // the trailing empty may no longer be trailing
    bumpGeneration();
    return true;
}

QString WorkspaceReconciler::transferWorkspace(const QString& desktopId, const QString& targetScreenId, int sliceIndex)
{
    const QString owner = m_map.ownerOf(desktopId);
    if (owner.isEmpty() || targetScreenId.isEmpty() || targetScreenId == owner || !m_map.knowsScreen(targetScreenId)) {
        return QString();
    }
    if (m_map.sliceSize(owner) <= 1) {
        return QString(); // a screen never gives up its last desktop
    }
    // Never after the target's trailing empty: that slot is the invariant,
    // and a workspace landing past it would make the empty a surplus.
    const int index = qBound(0, sliceIndex, insertIndexBeforeTrailingEmpty(targetScreenId));
    if (!m_map.transfer(desktopId, targetScreenId, index)) {
        return QString();
    }
    // A deliberate move overrides hotplug memory (see transferCurrentWorkspace).
    m_map.setHomeScreen(desktopId, QString());
    // The source screen must land on one of its own desktops when it was
    // showing the moved one; either slice may need trailing-empty repair.
    if (currentDesktopIdOf(owner) == desktopId) {
        snapBack(owner);
    }
    maintainScreen(owner);
    maintainScreen(targetScreenId);
    bumpGeneration();
    return desktopId;
}

bool WorkspaceReconciler::requestInsertWorkspace(const QString& screenId, int sliceIndex)
{
    if (!m_map.knowsScreen(screenId)) {
        return false;
    }
    const int index = qBound(0, sliceIndex, m_map.sliceSize(screenId));
    const int before = m_ledger.size();
    requestCreateAt(screenId, index, QString(), /*reserved=*/true);
    return m_ledger.size() > before;
}

void WorkspaceReconciler::releaseReservation(const QString& desktopId)
{
    if (!m_reservedDesktops.remove(desktopId)) {
        return;
    }
    // Back under the ordinary rules: an empty dynamic desktop that is not the
    // trailing one is surplus.
    const QString owner = m_map.ownerOf(desktopId);
    if (!owner.isEmpty()) {
        maintainScreen(owner);
    }
}

bool WorkspaceReconciler::requestRename(const QString& desktopId, const QString& name)
{
    if (m_map.ownerOf(desktopId).isEmpty()) {
        return false;
    }
    // Same ledger discipline as the declaration path: one push per name in
    // flight, bounded refusals so a rename KWin never accepts does not re-fire
    // for the life of the session.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto push = m_namePushes.constFind(desktopId);
    const bool sameName = push != m_namePushes.constEnd() && push->name == name;
    if (sameName && push->deadline > now) {
        return true;
    }
    const int refusals = sameName ? push->refusals + 1 : 0;
    if (refusals >= MaxNamePushRefusals) {
        return false;
    }
    m_namePushes.insert(desktopId, NamePush{name, now + LedgerTimeoutMs, refusals});
    Q_EMIT requestSetDesktopName(desktopId, name);
    return true;
}

} // namespace PhosphorWorkspaces
