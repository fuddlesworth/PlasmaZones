// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// WorkspaceController — verb execution (plan §4.2), split from
// workspacecontroller.cpp by concern. Every verb resolves its target desktop
// by UUID inside the reconciler and translates to the live int only at emit
// time, deferring behind the ledger while a structural op is in flight so a
// renumbering window can never hand a verb a stale index.

#include "workspacecontroller.h"

#include "config/configdefaults.h"

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorIdentity/WindowId.h>
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

bool WorkspaceController::watchWindowMove(const QString& windowId, const QString& targetDesktopId)
{
    // Sticky refusal, checked HERE so every move verb inherits it: a window on
    // all desktops is already on the target, and the adaptor's move slot drops
    // the request outright (crossmode.cpp). Arming a watch for an arrival that
    // can never come warned two seconds later. The callers skip their emit on
    // a false answer.
    if (m_windowStickyPredicate && m_windowStickyPredicate(windowId)) {
        qCInfo(lcWorkspaceCtl) << "workspace move for window" << windowId << "to desktop" << targetDesktopId
                               << "skipped: window is sticky, already on every workspace";
        return false;
    }
    // Sequence token per watch, not just the target id: two moves of the same
    // window to the same desktop (a repeated chord, a reunion following a
    // displacement) are indistinguishable by target, so the FIRST timer would
    // match the SECOND watch's entry, clear it and warn — while the second
    // move was still perfectly in flight, and its own arrival then had no
    // entry left to retire.
    // Keyed on the BARE instance id, not on whatever the verb was handed. The
    // shortcut verbs pass the registry-canonical COMPOSITE "appId|instanceId",
    // while the only thing that retires a watch - the census arrival in
    // onMetadataChanged - is keyed on the instance id the registry reports. A
    // composite-keyed entry therefore never retired: every successful chord
    // move warned two seconds later, and reuniteWindowWithOwner's in-flight
    // guard missed those moves and could fire a reunion mid-move. The instance
    // id is also the id that cannot change mid-life; the composite can (a
    // WM_CLASS-mutating app renames it).
    const QString watchKey = PhosphorIdentity::WindowId::extractInstanceId(windowId);
    const quint64 sequence = ++m_windowMoveSequence;
    m_pendingWindowMoves.insert(watchKey, targetDesktopId);
    m_windowMoveSequences.insert(watchKey, sequence);
    QTimer::singleShot(WindowMoveTimeoutMs, this, [this, watchKey, windowId, targetDesktopId, sequence]() {
        if (m_windowMoveSequences.value(watchKey) != sequence) {
            return; // superseded by a later watch (or already retired)
        }
        if (m_pendingWindowMoves.value(watchKey) == targetDesktopId) {
            m_pendingWindowMoves.remove(watchKey);
            m_windowMoveSequences.remove(watchKey);
            qCWarning(lcWorkspaceCtl) << "workspace move for window" << windowId << "to desktop" << targetDesktopId
                                      << "saw no arrival (effect not loaded, window closed, or handoff refused)";
        }
    });
    return true;
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
    if (m_draining || m_reconciler.hasPendingStructuralOps()) {
        return;
    }
    // Parked open-path routes drain here too, so the emptiness test covers
    // both queues. They are NOT put on m_quietQueue: that queue is ordered
    // against the user's verbs, while a parked route is a rule acting on one
    // window and has its own liveness check for a window that closed while
    // the reconciler was busy.
    if (m_quietQueue.isEmpty() && m_parkedNamedRoutes.isEmpty()) {
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
    // Inside the latch: a re-issued route can start churn of its own (the
    // move fills the destination, which cuts a new trailing empty there), and
    // that mapChanged comes straight back to this slot. Draining here means
    // the nested call is swallowed by m_draining rather than walking a
    // half-exchanged hash, and anything that re-parks waits for the next
    // quiet edge exactly as it would have.
    if (!m_reconciler.hasPendingStructuralOps()) {
        drainParkedNamedRoutes();
    }
    m_draining = false;
}

void WorkspaceController::switchScreenToDesktop(const QString& screenId, const QString& desktopId)
{
    // Refuse BEFORE the ledger, not after. issueSetCurrent ledgers the switch
    // and only then re-emits requestSetCurrent; if the translation to a live
    // desktop number fails there, the entry is already open and nothing can
    // retire it short of its own expiry, which short-circuits this screen's
    // foreign evaluation for the whole timeout and then fires a spurious
    // resync. Asking the same question here keeps a hopeless switch out of the
    // ledger entirely.
    if (m_vdm->desktopIndexOf(desktopId) <= 0) {
        qCWarning(lcWorkspaceCtl) << "not switching" << screenId << "to" << desktopId
                                  << "- that desktop has no live number yet";
        return;
    }
    // A refusal (a switch already in flight for this screen) is simply dropped
    // — the in-flight one wins, matching the snap-back single-correction rule.
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
        if (!watchWindowMove(windowId, target)) {
            return;
        }
        Q_EMIT windowWorkspaceMoveRequested(windowId, screenId, desktop, QString(),
                                            delta < 0 ? QStringLiteral("up") : QStringLiteral("down"),
                                            /*moveOutput=*/true);
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
            // Per window: a sticky member of the column is refused on its own
            // and the rest of the column still moves.
            if (!watchWindowMove(windowId, target)) {
                continue;
            }
            Q_EMIT windowWorkspaceMoveRequested(windowId, screenId, desktop, QString(), direction, /*moveOutput=*/true);
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
        // Public API: anything that is neither left nor right is a caller
        // error, not a synonym for right.
        if (direction != QLatin1String("left") && direction != QLatin1String("right")) {
            qCWarning(lcWorkspaceCtl) << "moveWorkspaceToOutput: unknown direction" << direction
                                      << "- expected left or right";
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
                // A sticky rider is on the moved desktop already and stays
                // put; the remaining riders still cross to the new output.
                if (!watchWindowMove(windowId, movedId)) {
                    continue;
                }
                Q_EMIT windowWorkspaceMoveRequested(windowId, targetScreen, desktop, QString(), direction,
                                                    /*moveOutput=*/true);
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
            // The settings UI stores an EFFECTIVE screen id: a "<phys>/vs:N"
            // virtual id on a subdivided output, or a bare connector name in
            // the Qt fallback. The map only ever knows physical,
            // effect-reported ids, so copying the raw value through silently
            // degraded every pin to unpinned. Same normalization every other
            // externally sourced screen id in this controller goes through.
            const QString rawOutput = map.value(ConfigDefaults::namedEntryOutputField()).toString().trimmed();
            decl.outputId = rawOutput.isEmpty()
                ? QString()
                : canonicalScreenId(PhosphorIdentity::VirtualScreenId::extractPhysicalId(rawOutput));
            if (!decl.outputId.isEmpty() && !m_reconciler.map().screenOrder().contains(decl.outputId)) {
                // Not fatal - the monitor may simply be unplugged, and the pin
                // takes effect when it returns - but silently unpinning was
                // indistinguishable from the id-space bug above.
                qCWarning(lcWorkspaceCtl) << "named workspace" << decl.name << "pins output" << rawOutput
                                          << "(resolved to" << decl.outputId << ") which is not a known screen";
            }
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
    // Tie-break: the FIRST match in the map's canonical desktop-id order wins,
    // and a second match is reported. The declaration side cannot produce one
    // - WorkspaceReconciler::applyNamedWorkspaces skips a duplicate trimmed
    // name outright and realizes each name onto exactly one id - so a
    // duplicate here means two entries picked up the same name from KWin's own
    // desktop names, which is the user's to resolve. Deterministic and loud
    // beats first-hit-and-hope.
    const QStringList ids = m_reconciler.map().allDesktopIds();
    QString match;
    for (const QString& id : ids) {
        if (m_reconciler.map().entryFor(id).name != name) {
            continue;
        }
        if (match.isEmpty()) {
            match = id;
            continue;
        }
        qCWarning(lcWorkspaceCtl) << "more than one workspace is named" << name << "- using" << match << "and ignoring"
                                  << id;
        break;
    }
    return match;
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

int WorkspaceController::routeWindowToNamedWorkspace(const QString& name, const QString& windowId, bool moveOutput,
                                                     QString* ownerScreenOut)
{
    // Rules-pipeline arm: no runWhenQuiet — the caller needs the truth NOW.
    if (windowId.isEmpty()) {
        return static_cast<int>(WorkspaceRouteVerdict::Unrealized);
    }
    // Before adoption the map is the PREVIOUS session's candidate, so
    // desktopIdForName refuses - and that window is exactly the login /
    // session-restore population these rules are written for. Park the request
    // and re-issue it once adoption completes rather than dropping it, and
    // report Unresolvable so the positional route does not move the window in
    // the meantime (it would then be moved twice, to two different desktops).
    if (!m_adopted) {
        m_parkedNamedRoutes.insert(PhosphorIdentity::WindowId::extractInstanceId(windowId),
                                   ParkedNamedRoute{name, moveOutput});
        return static_cast<int>(WorkspaceRouteVerdict::Unresolvable);
    }
    // During structural churn the slice index a deferred move would resolve
    // later cannot be promised. That is NOT the same as an undeclared name:
    // the positional route is the author's fallback for a name this session
    // does not have, and substituting it here would land the window on a
    // different desktop for the duration of a transient reconciler op.
    //
    // Parked rather than simply refused, because this is the COMMON case, not
    // a rare one: a window arriving is itself what fills a workspace, and a
    // filled workspace is what makes the reconciler cut a new trailing empty.
    // So the op in flight is usually the one this very window started, and
    // answering "unresolvable" alone meant an open-path rule reliably did
    // nothing at exactly the moment it was supposed to act. The park re-issues
    // it once the reconciler is quiet, on the same queue and with the same
    // registry liveness check the pre-adoption park already uses. The verdict
    // stays Unresolvable so the caller still does not substitute the
    // positional route in the meantime.
    if (m_reconciler.hasPendingStructuralOps()) {
        m_parkedNamedRoutes.insert(PhosphorIdentity::WindowId::extractInstanceId(windowId),
                                   ParkedNamedRoute{name, moveOutput});
        return static_cast<int>(WorkspaceRouteVerdict::Unresolvable);
    }
    const QString target = desktopIdForName(name);
    if (target.isEmpty()) {
        return static_cast<int>(WorkspaceRouteVerdict::Unrealized);
    }
    const int desktop = m_vdm->desktopIndexOf(target);
    if (desktop <= 0) {
        // The name IS realized; only its live number is momentarily missing
        // (a settled-list echo still in flight). Same reasoning as the
        // structural-churn arm above.
        return static_cast<int>(WorkspaceRouteVerdict::Unresolvable);
    }
    const QString owner = m_reconciler.map().ownerOf(target);
    // A sticky refusal (watchWindowMove answering false) reports the REALIZED
    // desktop so the caller does not fall back to an unrelated positional
    // route, but deliberately does NOT report an owner screen. The window is
    // on every workspace and no move of any kind is issued, so it stays on the
    // output it opened on — and the caller pins the placement directive to
    // whatever owner screen it is handed. Reporting one here resolved a sticky
    // window's zones on a monitor it never reaches.
    if (!watchWindowMove(windowId, target)) {
        return desktop;
    }
    if (ownerScreenOut) {
        *ownerScreenOut = owner;
    }
    // The owner screen is always carried so the handoff re-homes engine state
    // on the right output; @p moveOutput decides whether the OUTPUT leg is
    // issued as well (see the header).
    Q_EMIT windowWorkspaceMoveRequested(windowId, owner, desktop, target, QStringLiteral("down"), moveOutput);
    return desktop;
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
        if (!watchWindowMove(windowId, target)) {
            return;
        }
        Q_EMIT windowWorkspaceMoveRequested(windowId, m_reconciler.map().ownerOf(target), desktop, target,
                                            QStringLiteral("down"), /*moveOutput=*/true);
    });
}

void WorkspaceController::drainParkedNamedRoutes()
{
    if (m_parkedNamedRoutes.isEmpty()) {
        return;
    }
    const QHash<QString, ParkedNamedRoute> parked = std::exchange(m_parkedNamedRoutes, {});
    for (auto it = parked.constBegin(); it != parked.constEnd(); ++it) {
        // Windows that went away while adoption was pending have nothing to
        // route. The registry is keyed by instance id, which is how the park
        // stored them.
        if (!m_registry || !m_registry->metadata(it.key())) {
            continue;
        }
        qCInfo(lcWorkspaceCtl) << "re-issuing the parked workspace route for" << it.key() << "to" << it.value().name;
        routeWindowToNamedWorkspace(it.value().name, it.key(), it.value().moveOutput);
    }
}

} // namespace PlasmaZones
