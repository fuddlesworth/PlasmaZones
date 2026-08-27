// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// WorkspaceController — verb execution (plan §4.2), split from
// workspacecontroller.cpp by concern. Every verb resolves its target desktop
// by UUID inside the reconciler and translates to the live int only at emit
// time, deferring behind the ledger while a structural op is in flight so a
// renumbering window can never hand a verb a stale index.

#include "workspacecontroller.h"

#include <PhosphorWorkspaces/VirtualDesktopManager.h>

#include <QLoggingCategory>

#include <utility>

namespace PlasmaZones {

void WorkspaceController::runWhenQuiet(std::function<void()> fn)
{
    if (!m_reconciler.hasPendingStructuralOps()) {
        fn();
        return;
    }
    m_quietQueue.append(std::move(fn));
}

void WorkspaceController::drainQuietQueue()
{
    if (m_quietQueue.isEmpty() || m_reconciler.hasPendingStructuralOps()) {
        return;
    }
    const auto queue = std::exchange(m_quietQueue, {});
    for (const auto& fn : queue) {
        fn();
    }
}

void WorkspaceController::switchScreenToDesktop(const QString& screenId, const QString& desktopId)
{
    // The reconciler ledgers the switch and re-emits requestSetCurrent, which
    // the ctor wiring translates to the effect command. A refusal (a switch
    // already in flight for this screen) is simply dropped — the in-flight
    // one wins, matching the snap-back single-correction rule.
    m_reconciler.issueSetCurrent(screenId, desktopId);
}

void WorkspaceController::focusWorkspace(const QString& screenId, int delta)
{
    runWhenQuiet([this, screenId, delta]() {
        const QString target = m_reconciler.desktopIdAtOffset(screenId, delta);
        if (target.isEmpty()) {
            return; // slice edge: no wrap
        }
        switchScreenToDesktop(screenId, target);
    });
}

void WorkspaceController::focusWorkspaceAt(const QString& screenId, int sliceIndex)
{
    runWhenQuiet([this, screenId, sliceIndex]() {
        const QString target = m_reconciler.desktopIdAtSliceIndex(screenId, sliceIndex);
        if (target.isEmpty()) {
            return;
        }
        switchScreenToDesktop(screenId, target);
    });
}

void WorkspaceController::moveWindowToWorkspace(const QString& screenId, const QString& windowId, int delta)
{
    if (windowId.isEmpty()) {
        return;
    }
    runWhenQuiet([this, screenId, windowId, delta]() {
        const QString target = m_reconciler.desktopIdAtOffset(screenId, delta);
        if (target.isEmpty()) {
            return;
        }
        const int desktop = m_vdm->desktopIndexOf(target);
        if (desktop <= 0) {
            return;
        }
        Q_EMIT windowWorkspaceMoveRequested(windowId, screenId, desktop,
                                            delta < 0 ? QStringLiteral("up") : QStringLiteral("down"));
    });
}

void WorkspaceController::moveWindowToWorkspaceAt(const QString& screenId, const QString& windowId, int sliceIndex)
{
    if (windowId.isEmpty()) {
        return;
    }
    runWhenQuiet([this, screenId, windowId, sliceIndex]() {
        const QString target = m_reconciler.desktopIdAtSliceIndex(screenId, sliceIndex);
        if (target.isEmpty()) {
            return;
        }
        const int desktop = m_vdm->desktopIndexOf(target);
        if (desktop <= 0) {
            return;
        }
        Q_EMIT windowWorkspaceMoveRequested(windowId, screenId, desktop, QStringLiteral("down"));
    });
}

void WorkspaceController::moveColumnToWorkspace(const QString& screenId, const QStringList& columnWindows, int delta)
{
    if (columnWindows.isEmpty()) {
        return;
    }
    runWhenQuiet([this, screenId, columnWindows, delta]() {
        const QString target = m_reconciler.desktopIdAtOffset(screenId, delta);
        if (target.isEmpty()) {
            return;
        }
        const int desktop = m_vdm->desktopIndexOf(target);
        if (desktop <= 0) {
            return;
        }
        const QString direction = delta < 0 ? QStringLiteral("up") : QStringLiteral("down");
        // Group semantics ride the per-window handoff: the scroll engine's
        // handoffReceive re-forms the column on the target strip in arrival
        // order (same contract the monitor-crossing column moves rely on).
        for (const QString& windowId : columnWindows) {
            Q_EMIT windowWorkspaceMoveRequested(windowId, screenId, desktop, direction);
        }
    });
}

void WorkspaceController::moveWorkspace(const QString& screenId, int delta)
{
    runWhenQuiet([this, screenId, delta]() {
        m_reconciler.reorderCurrentWorkspace(screenId, delta);
    });
}

void WorkspaceController::moveWorkspaceToOutput(const QString& screenId, const QString& direction)
{
    runWhenQuiet([this, screenId, direction]() {
        // Neighbour in screen order (geometry left-to-right, the same order
        // the slices concatenate in).
        const QStringList order = m_reconciler.map().screenOrder();
        const int index = order.indexOf(screenId);
        if (index < 0) {
            return;
        }
        const int neighbourIndex = direction == QLatin1String("left") ? index - 1 : index + 1;
        if (neighbourIndex < 0 || neighbourIndex >= order.size()) {
            return;
        }
        const QString targetScreen = order.at(neighbourIndex);

        // Windows riding along are enumerated BEFORE the transfer (the census
        // keys by desktop id, unaffected by the map mutation).
        const QString movingId = m_reconciler.currentDesktopIdOf(screenId);
        QStringList riders;
        for (auto it = m_windowCensusDesktopId.constBegin(); it != m_windowCensusDesktopId.constEnd(); ++it) {
            if (it.value() == movingId) {
                riders.append(it.key());
            }
        }

        const QString movedId = m_reconciler.transferCurrentWorkspace(screenId, targetScreen);
        if (movedId.isEmpty()) {
            return;
        }
        const int desktop = m_vdm->desktopIndexOf(movedId);
        if (desktop > 0) {
            // The desktop keeps its identity; the windows change OUTPUT. The
            // handoff verb re-homes each one's engine state and geometry on
            // the target screen (same desktop int).
            for (const QString& windowId : riders) {
                Q_EMIT windowWorkspaceMoveRequested(windowId, targetScreen, desktop, direction);
            }
            // niri semantics: the moved workspace gains focus on its new
            // output (the source already snapped back inside the transfer).
            m_reconciler.issueSetCurrent(targetScreen, movedId);
        }
    });
}

} // namespace PlasmaZones
