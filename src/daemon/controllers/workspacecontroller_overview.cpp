// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The by-id verbs the workspace overview drives. Same shape as the delta
// verbs in workspacecontroller_verbs.cpp (defer behind the ledger, walk the
// screen's own slice, refuse quietly), keyed by desktop id rather than by
// offset from the current workspace.

#include "workspacecontroller.h"

#include <PhosphorWorkspaces/VirtualDesktopManager.h>

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcWorkspaceCtl)

namespace PlasmaZones {

void WorkspaceController::focusWorkspaceById(const QString& screenId, const QString& desktopId)
{
    runWhenQuiet([this, screenId, desktopId]() {
        if (m_reconciler.map().ownerOf(desktopId) != screenId) {
            qCDebug(lcWorkspaceCtl) << "focusWorkspaceById: " << desktopId << "is not owned by" << screenId;
            return;
        }
        switchScreenToDesktop(screenId, desktopId);
    });
}

void WorkspaceController::moveWindowToWorkspaceById(const QString& windowId, const QString& screenId,
                                                    const QString& desktopId,
                                                    const PhosphorEngine::HandoffIntent& intent)
{
    if (windowId.isEmpty()) {
        return;
    }
    runWhenQuiet([this, windowId, screenId, desktopId, intent]() {
        if (m_reconciler.map().ownerOf(desktopId) != screenId) {
            qCDebug(lcWorkspaceCtl) << "moveWindowToWorkspaceById:" << desktopId << "is not owned by" << screenId;
            return;
        }
        const int desktop = m_vdm->desktopIndexOf(desktopId);
        if (desktop <= 0) {
            return;
        }
        if (!watchWindowMove(windowId, desktopId)) {
            return;
        }
        Q_EMIT windowWorkspaceMoveWithIntentRequested(windowId, screenId, desktop, desktopId, intent);
    });
}

void WorkspaceController::moveWindowToNewWorkspace(const QString& windowId, const QString& screenId, int sliceIndex,
                                                   const PhosphorEngine::HandoffIntent& intent)
{
    if (windowId.isEmpty()) {
        return;
    }
    runWhenQuiet([this, windowId, screenId, sliceIndex, intent]() {
        if (!m_reconciler.map().knowsScreen(screenId)) {
            qCDebug(lcWorkspaceCtl) << "moveWindowToNewWorkspace: unknown screen" << screenId;
            return;
        }
        const int size = m_reconciler.map().sliceSize(screenId);
        // niri: a gap at or past the trailing empty reuses that workspace
        // instead of inserting one beside it.
        const QString trailing = m_reconciler.trailingEmptyOf(screenId);
        if (!trailing.isEmpty() && sliceIndex >= size - 1) {
            moveWindowToWorkspaceById(windowId, screenId, trailing, intent);
            return;
        }
        const int index = qBound(0, sliceIndex, size);
        if (!m_reconciler.requestInsertWorkspace(screenId, index)) {
            qCDebug(lcWorkspaceCtl) << "moveWindowToNewWorkspace: create refused for" << screenId;
            return;
        }
        // The create is in flight; the move runs once the ledger quiets and
        // the new desktop sits at the requested slot. A new scrolling
        // workspace always receives the window as its first column.
        PhosphorEngine::HandoffIntent newIntent = intent;
        newIntent.insertIndex = 0;
        newIntent.insertTileIndex = -1;
        runWhenQuiet([this, windowId, screenId, index, newIntent]() {
            const QString created = m_reconciler.desktopIdAtSliceIndex(screenId, index);
            if (created.isEmpty() || !m_reconciler.isReserved(created)) {
                qCDebug(lcWorkspaceCtl) << "moveWindowToNewWorkspace: no reserved workspace landed at" << index << "on"
                                        << screenId;
                return;
            }
            const int desktop = m_vdm->desktopIndexOf(created);
            if (desktop <= 0 || !watchWindowMove(windowId, created)) {
                m_reconciler.releaseReservation(created);
                return;
            }
            Q_EMIT windowWorkspaceMoveWithIntentRequested(windowId, screenId, desktop, created, newIntent);
        });
    });
}

bool WorkspaceController::reorderWorkspaceById(const QString& screenId, const QString& desktopId, int newSliceIndex)
{
    if (m_reconciler.hasPendingStructuralOps() || m_reconciler.map().ownerOf(desktopId) != screenId) {
        qCDebug(lcWorkspaceCtl) << "reorderWorkspaceById: refused for" << desktopId << "on" << screenId;
        return false;
    }
    return m_reconciler.reorderWorkspace(desktopId, newSliceIndex);
}

bool WorkspaceController::moveWorkspaceToScreenById(const QString& desktopId, const QString& targetScreenId,
                                                    int sliceIndex)
{
    if (m_reconciler.hasPendingStructuralOps()) {
        return false;
    }
    const QStringList order = m_reconciler.map().screenOrder();
    const QString owner = m_reconciler.map().ownerOf(desktopId);
    const int from = order.indexOf(owner);
    const int to = order.indexOf(targetScreenId);
    if (from < 0 || to < 0) {
        qCDebug(lcWorkspaceCtl) << "moveWorkspaceToScreenById: refused for" << desktopId << "to" << targetScreenId;
        return false;
    }
    const QStringList riders = windowsOnWorkspace(desktopId);
    const QString movedId = m_reconciler.transferWorkspace(desktopId, targetScreenId, sliceIndex);
    if (movedId.isEmpty()) {
        return false;
    }
    relocateRidersAndShow(riders, movedId, targetScreenId,
                          to < from ? QStringLiteral("left") : QStringLiteral("right"));
    return true;
}

bool WorkspaceController::renameDynamicWorkspace(const QString& desktopId, const QString& name)
{
    return m_reconciler.requestRename(desktopId, name);
}

} // namespace PlasmaZones
