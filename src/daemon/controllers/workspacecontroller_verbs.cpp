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
#include <QTimer>

#include <utility>

Q_DECLARE_LOGGING_CATEGORY(lcWorkspaceCtl)

namespace PlasmaZones {

namespace {
/// How long a move verb waits for its census arrival before the watchdog
/// declares it lost (effect unloaded, window gone, refusal upstream).
constexpr int WindowMoveTimeoutMs = 2000;
}

void WorkspaceController::watchWindowMove(const QString& windowId, const QString& targetDesktopId)
{
    m_pendingWindowMoves.insert(windowId, targetDesktopId);
    QTimer::singleShot(WindowMoveTimeoutMs, this, [this, windowId, targetDesktopId]() {
        if (m_pendingWindowMoves.value(windowId) == targetDesktopId) {
            m_pendingWindowMoves.remove(windowId);
            qCWarning(lcWorkspaceCtl) << "workspace move for window" << windowId << "to desktop" << targetDesktopId
                                      << "saw no arrival (effect not loaded, window closed, or handoff refused)";
        }
    });
}

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
        watchWindowMove(windowId, target);
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
        watchWindowMove(windowId, target);
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
            watchWindowMove(windowId, target);
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
                watchWindowMove(windowId, movedId);
                Q_EMIT windowWorkspaceMoveRequested(windowId, targetScreen, desktop, direction);
            }
            // niri semantics: the moved workspace gains focus on its new
            // output (the source already snapped back inside the transfer).
            m_reconciler.issueSetCurrent(targetScreen, movedId);
        }
    });
}

// ── Named workspaces ────────────────────────────────────────────────────────

void WorkspaceController::applyNamedDeclarations(const QVariantList& entries)
{
    m_namedEntries = entries;
    if (!m_adopted) {
        return; // re-applied by start()/adoption completion
    }
    m_namedApplied = true;
    runWhenQuiet([this]() {
        QList<PhosphorWorkspaces::NamedWorkspace> declarations;
        declarations.reserve(m_namedEntries.size());
        for (const QVariant& value : std::as_const(m_namedEntries)) {
            const QVariantMap map = value.toMap();
            PhosphorWorkspaces::NamedWorkspace decl;
            decl.name = map.value(QStringLiteral("name")).toString().trimmed();
            decl.outputId = map.value(QStringLiteral("output")).toString();
            decl.position = map.value(QStringLiteral("position"), -1).toInt();
            declarations.append(decl);
        }
        m_reconciler.applyNamedWorkspaces(declarations, m_vdm->desktopNames());
    });
}

QString WorkspaceController::desktopIdForName(const QString& name) const
{
    const QStringList ids = m_reconciler.map().allDesktopIds();
    for (const QString& id : ids) {
        if (m_reconciler.map().entryFor(id).name == name) {
            return id;
        }
    }
    return QString();
}

void WorkspaceController::focusNamedWorkspace(const QString& name)
{
    runWhenQuiet([this, name]() {
        const QString target = desktopIdForName(name);
        if (target.isEmpty()) {
            return;
        }
        // A named workspace shows where it LIVES: the switch targets its
        // owner screen, whichever screen the shortcut fired on.
        switchScreenToDesktop(m_reconciler.map().ownerOf(target), target);
    });
}

void WorkspaceController::moveWindowToNamedWorkspace(const QString& name, const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    runWhenQuiet([this, name, windowId]() {
        const QString target = desktopIdForName(name);
        if (target.isEmpty()) {
            return;
        }
        const int desktop = m_vdm->desktopIndexOf(target);
        if (desktop <= 0) {
            return;
        }
        watchWindowMove(windowId, target);
        Q_EMIT windowWorkspaceMoveRequested(windowId, m_reconciler.map().ownerOf(target), desktop,
                                            QStringLiteral("down"));
    });
}

} // namespace PlasmaZones
