// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// WorkspaceController — verb execution (plan §4.2), split from
// workspacecontroller.cpp by concern. Every verb resolves its target desktop
// by UUID inside the reconciler and translates to the live int only at emit
// time, deferring behind the ledger while a structural op is in flight so a
// renumbering window can never hand a verb a stale index.

#include "workspacecontroller.h"

#include "config/configdefaults.h"

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
    // Sequence token per watch, not just the target id: two moves of the same
    // window to the same desktop (a repeated chord, a reunion following a
    // displacement) are indistinguishable by target, so the FIRST timer would
    // match the SECOND watch's entry, clear it and warn — while the second
    // move was still perfectly in flight, and its own arrival then had no
    // entry left to retire.
    const quint64 sequence = ++m_windowMoveSequence;
    m_pendingWindowMoves.insert(windowId, targetDesktopId);
    m_windowMoveSequences.insert(windowId, sequence);
    QTimer::singleShot(WindowMoveTimeoutMs, this, [this, windowId, targetDesktopId, sequence]() {
        if (m_windowMoveSequences.value(windowId) != sequence) {
            return; // superseded by a later watch (or already retired)
        }
        if (m_pendingWindowMoves.value(windowId) == targetDesktopId) {
            m_pendingWindowMoves.remove(windowId);
            m_windowMoveSequences.remove(windowId);
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
    // Re-entrancy: a drained verb emits mapChanged, which is wired straight
    // back to this slot. Without the latch the nested call would walk the
    // member queue a second time and run the re-queued remainder out of order.
    if (m_draining || m_quietQueue.isEmpty() || m_reconciler.hasPendingStructuralOps()) {
        return;
    }
    m_draining = true;
    // Looped rather than one pass: the mapChanged a drained verb emits is
    // swallowed by the latch above, so the condition has to be re-tested here
    // or work queued (or unblocked) during the batch would strand until an
    // unrelated later signal.
    while (!m_quietQueue.isEmpty() && !m_reconciler.hasPendingStructuralOps()) {
        const auto queue = std::exchange(m_quietQueue, {});
        for (int i = 0; i < queue.size(); ++i) {
            // Re-checked per verb, not once for the batch. A drained verb can
            // START structural churn of its own (a reorder or an output
            // transfer that makes the reconciler create or remove a desktop),
            // and every verb after it in the batch would then resolve its
            // slice index against a map mid-renumber — the exact stale-index
            // window runWhenQuiet exists to keep verbs out of.
            if (m_reconciler.hasPendingStructuralOps()) {
                QList<std::function<void()>> rest = queue.mid(i);
                // Anything a drained verb queued re-entrantly landed in the
                // member queue and belongs AFTER the remainder, in arrival
                // order.
                rest.append(m_quietQueue);
                m_quietQueue = std::move(rest);
                break;
            }
            queue.at(i)();
        }
    }
    m_draining = false;
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
            return; // slot beyond the screen's slice
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
            decl.name = map.value(ConfigDefaults::namedEntryNameField()).toString().trimmed();
            decl.outputId = map.value(ConfigDefaults::namedEntryOutputField()).toString();
            decl.position = map.value(ConfigDefaults::namedEntryPositionField(), -1).toInt();
            declarations.append(decl);
        }
        // rawDesktopNames, NOT desktopNames: this is the IDENTITY path, and it
        // needs KWin's names verbatim with an empty string for an unnamed
        // desktop. desktopNames() fills "Desktop N" placeholders for DISPLAY
        // callers, and matching against those let a workspace literally named
        // "Desktop 3" claim an unnamed desktop — and then let the kwinAgrees
        // tiebreak carry that identity across a renumber.
        m_reconciler.applyNamedWorkspaces(declarations, m_vdm->rawDesktopNames());
    });
}

bool WorkspaceController::hasNamedWorkspace(const QString& name) const
{
    return !name.isEmpty() && !desktopIdForName(name).isEmpty();
}

QString WorkspaceController::desktopIdForName(const QString& name) const
{
    // Adoption gate. Before adoption the map is NOT empty — start() loads the
    // previous session's candidate map from the state file, names and all —
    // so a name resolves to a desktop id that has not been reconciled against
    // the live compositor yet. Routing on it sends the window to whatever
    // position that id happens to occupy in this session's list. Every caller
    // treats an empty answer as "name unrealized" and falls back, which is
    // the right behaviour for the pre-adoption window.
    if (!m_adopted) {
        return QString();
    }
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
        const QString owner = m_reconciler.map().ownerOf(target);
        if (owner.isEmpty()) {
            // Ownership has not settled. Unlike the MOVE verbs, whose adaptor
            // degrades an empty target screen to the window's own output,
            // there is nothing to degrade to here: a per-screen switch with no
            // screen would take a ledger slot and answer nothing.
            qCWarning(lcWorkspaceCtl) << "focus named workspace" << name << ": no owner screen yet, ignoring";
            return;
        }
        switchScreenToDesktop(owner, target);
    });
}

bool WorkspaceController::routeWindowToNamedWorkspace(const QString& name, const QString& windowId)
{
    // Rules-pipeline arm: no runWhenQuiet — the caller needs the truth NOW.
    // During structural churn the slice index a deferred move would resolve
    // later cannot be promised, so report false and let the positional
    // desktop route (if the cascade carries one) handle the window instead.
    if (windowId.isEmpty() || m_reconciler.hasPendingStructuralOps()) {
        return false;
    }
    const QString target = desktopIdForName(name);
    if (target.isEmpty()) {
        return false;
    }
    const int desktop = m_vdm->desktopIndexOf(target);
    if (desktop <= 0) {
        return false;
    }
    // A sticky window is on every workspace already, and the adaptor's move
    // slot refuses it outright (crossmode.cpp: the effect drops the desktop
    // move for an on-all-desktops window). Emitting anyway armed a watchdog
    // for an arrival that could never come, which warned two seconds later.
    // TRUE, not false: the rule asked for the window to be on that
    // workspace and it is, so the positional RouteToDesktop fallback would
    // be refused for the very same reason.
    if (m_windowStickyPredicate && m_windowStickyPredicate(windowId)) {
        qCInfo(lcWorkspaceCtl) << "route to named workspace" << name << ": window" << windowId
                               << "is sticky, already on every workspace";
        return true;
    }
    watchWindowMove(windowId, target);
    Q_EMIT windowWorkspaceMoveRequested(windowId, m_reconciler.map().ownerOf(target), desktop, QStringLiteral("down"));
    return true;
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
        // Same sticky refusal as routeWindowToNamedWorkspace: the adaptor
        // drops the move, so arming the watchdog only buys a spurious
        // "saw no arrival" warning two seconds later.
        if (m_windowStickyPredicate && m_windowStickyPredicate(windowId)) {
            qCInfo(lcWorkspaceCtl) << "move to named workspace" << name << ": window" << windowId
                                   << "is sticky, already on every workspace";
            return;
        }
        watchWindowMove(windowId, target);
        Q_EMIT windowWorkspaceMoveRequested(windowId, m_reconciler.map().ownerOf(target), desktop,
                                            QStringLiteral("down"));
    });
}

} // namespace PlasmaZones
