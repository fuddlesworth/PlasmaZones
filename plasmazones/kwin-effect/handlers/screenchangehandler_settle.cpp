// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ScreenChangeHandler: the settle of an output change.
//
// When an output goes away KWin moves its windows to another one, and when it
// comes back KWin returns the ones it remembers. Neither is the user's move, so
// a crossing seen during a screen change is deferred, and at the settle every
// window KWin moved is reported to the daemon, which parked what each engine
// held of the windows on the output that went away. The daemon answers with a
// verdict per window, and only the crossings it calls user moves replay here.

#include "screenchangehandler.h"
#include "dragtracker.h"
#include "snaphandler.h"
#include "tilinghandler/tilinghandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <core/output.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcScreenChange)

namespace PlasmaZones {

using PhosphorProtocol::OutputSettleRow;
using PhosphorProtocol::OutputSettleVerdict;
namespace VirtualScreenId = PhosphorIdentity::VirtualScreenId;

namespace {

bool outputConnected(const QString& uuid)
{
    if (uuid.isEmpty()) {
        return false;
    }
    for (const KWin::LogicalOutput* output : KWin::effects->screens()) {
        if (output && output->uuid() == uuid) {
            return true;
        }
    }
    return false;
}

bool isLiveOutput(const KWin::LogicalOutput* output)
{
    return output && KWin::effects->screens().contains(const_cast<KWin::LogicalOutput*>(output));
}

} // namespace

ScreenChangeHandler::WindowSnapshot ScreenChangeHandler::snapshotOf(KWin::EffectWindow* w) const
{
    WindowSnapshot snapshot;
    KWin::Window* const kw = w ? w->window() : nullptr;
    if (!kw) {
        return snapshot;
    }
    // moveResizeOutput: where KWin has put the window, committed or not. Its
    // restore moves a window with a configure the client acks later.
    const KWin::LogicalOutput* const output = kw->moveResizeOutput();
    if (output) {
        snapshot.outputUuid = output->uuid();
    }
    // An output being removed is no longer listed, and resolving its id now
    // would record a spelling under a pointer about to be freed: the window's
    // tracked screen names it as it was published.
    snapshot.screenId = isLiveOutput(output)
        ? m_effect->outputScreenId(output)
        : VirtualScreenId::extractPhysicalId(m_effect->m_trackedScreenPerWindow.value(w));
    snapshot.frame = kw->moveResizeGeometry().toRect();
    snapshot.moveResizeCount = int(kw->interactiveMoveResizeCount());
    snapshot.maximizeMode = int(kw->requestedMaximizeMode());
    snapshot.quickTileMode = kw->requestedQuickTileMode().toInt();
    snapshot.fullscreen = kw->isFullScreen();
    return snapshot;
}

bool ScreenChangeHandler::onPlaceholderOutput(KWin::EffectWindow* w) const
{
    KWin::Window* const kw = w ? w->window() : nullptr;
    return kw && kw->moveResizeOutput() && kw->moveResizeOutput()->isPlaceholder();
}

void ScreenChangeHandler::takeBaseline()
{
    m_settleBaseline.clear();
    for (KWin::EffectWindow* w : KWin::effects->stackingOrder()) {
        if (!w || w->isDeleted() || !m_effect->shouldHandleWindow(w, nullptr, /*exemptFullscreen=*/true)) {
            continue;
        }
        m_settleBaseline.insert(w, snapshotOf(w));
    }
}

void ScreenChangeHandler::captureEvacuees(KWin::LogicalOutput* output, const QString& screenId)
{
    // KWin's placeholder (every real output gone) is never a place a window
    // was parked for: the daemon has no screen for it (F729).
    if (m_stopped || !output || output->isPlaceholder() || output->uuid().isEmpty()) {
        return;
    }
    const QString uuid = output->uuid();
    const auto record = [&](KWin::EffectWindow* w) {
        if (!w || w->isDeleted() || !m_effect->shouldHandleWindow(w, nullptr, /*exemptFullscreen=*/true)) {
            return;
        }
        QHash<QString, EvacueeRecord>& records = m_evacueeRecords[w];
        // Once per output while it lives: a second removal of the same output
        // (KWin re-adds then drops it in one sleep) must not replace the
        // state from before the first with KWin's restore of it (F709).
        if (records.contains(uuid)) {
            return;
        }
        EvacueeRecord entry;
        entry.state = snapshotOf(w);
        entry.state.outputUuid = uuid;
        entry.state.screenId = screenId;
        records.insert(uuid, entry);
    };
    for (KWin::EffectWindow* w : KWin::effects->stackingOrder()) {
        if (w && w->window() && w->window()->moveResizeOutput() == output) {
            record(w);
        }
    }
    // A parked strip column sits off every output, and KWin may count it on a
    // neighbour: it is the strip's all the same (F769).
    for (const QString& windowId : m_effect->m_tilingHandler->stripMembersOn(screenId)) {
        record(m_effect->findWindowByIdExact(windowId));
    }
    qCInfo(lcScreenChange) << "Output" << screenId << "went away; recorded"
                           << std::count_if(m_evacueeRecords.cbegin(), m_evacueeRecords.cend(),
                                            [&uuid](const QHash<QString, EvacueeRecord>& records) {
                                                return records.contains(uuid);
                                            })
                           << "window(s) on it";
}

void ScreenChangeHandler::noteOutputAdded(KWin::LogicalOutput* output)
{
    if (m_stopped || !output || output->isPlaceholder() || output->uuid().isEmpty()) {
        return;
    }
    const QString uuid = output->uuid();
    for (QHash<QString, EvacueeRecord>& records : m_evacueeRecords) {
        const auto it = records.find(uuid);
        if (it != records.end()) {
            it->classified = false;
        }
    }
}

void ScreenChangeHandler::deferCrossing(KWin::EffectWindow* w, const QString& oldScreenId)
{
    if (!w || oldScreenId.isEmpty() || m_deferredCrossings.contains(w)) {
        return;
    }
    m_deferredCrossings.insert(w, oldScreenId);
    qCDebug(lcScreenChange) << "Deferring the crossing of" << m_effect->getWindowId(w) << "from" << oldScreenId
                            << "to the settle";
}

bool ScreenChangeHandler::holdsUnclassifiedRecord(KWin::EffectWindow* w) const
{
    const auto it = m_evacueeRecords.constFind(w);
    if (it == m_evacueeRecords.constEnd()) {
        return false;
    }
    KWin::Window* const kw = w->window();
    if (!kw || !kw->moveResizeOutput()) {
        return false;
    }
    const auto entry = it->constFind(kw->moveResizeOutput()->uuid());
    return entry != it->constEnd() && !entry->classified;
}

void ScreenChangeHandler::noteSkippedAnnounce(KWin::EffectWindow* w)
{
    if (w) {
        m_skippedAnnounces.insert(w);
    }
}

void ScreenChangeHandler::forgetWindow(KWin::EffectWindow* w)
{
    m_evacueeRecords.remove(w);
    m_settleBaseline.remove(w);
    m_deferredCrossings.remove(w);
    m_skippedAnnounces.remove(w);
}

void ScreenChangeHandler::dropEvacueeRecords()
{
    // The parks died with the daemon. Without this the skip would keep
    // returned windows out of every announce with nothing left to re-seat
    // them, and every settle would report them again (F767). A skipped
    // announce goes with them: the next daemon's bring-up announces every
    // window.
    m_evacueeRecords.clear();
    m_skippedAnnounces.clear();
}

void ScreenChangeHandler::slotParkDropped(const QString& windowId, const QString& outputUuid)
{
    KWin::EffectWindow* const w = m_effect->findWindowByIdExact(windowId);
    const auto it = w ? m_evacueeRecords.find(w) : m_evacueeRecords.end();
    if (it == m_evacueeRecords.end()) {
        return;
    }
    // No uuid: the daemon never learned which output the park was for, so
    // nothing narrower than the window can be named.
    if (outputUuid.isEmpty()) {
        m_evacueeRecords.erase(it);
        return;
    }
    it->remove(outputUuid);
    if (it->isEmpty()) {
        m_evacueeRecords.erase(it);
    }
}

PhosphorProtocol::OutputSettleRowList ScreenChangeHandler::buildSettleRows() const
{
    PhosphorProtocol::OutputSettleRowList rows;
    // A resolution or arrangement change alone moves no window between
    // outputs, and its crossings replay as moves without a verdict.
    if (!m_outputSetChanged) {
        return rows;
    }
    const QString dragged = m_effect->m_dragTracker && m_effect->m_dragTracker->isDragging()
        ? m_effect->m_dragTracker->draggedWindowId()
        : QString();
    for (KWin::EffectWindow* w : KWin::effects->stackingOrder()) {
        if (!w || w->isDeleted() || !w->window()) {
            continue;
        }
        const auto baseIt = m_settleBaseline.constFind(w);
        const auto recordsIt = m_evacueeRecords.constFind(w);
        const bool hasRecords = recordsIt != m_evacueeRecords.constEnd() && !recordsIt->isEmpty();
        if (baseIt == m_settleBaseline.constEnd() && !hasRecords) {
            continue;
        }
        // A window on KWin's placeholder is reported once a real output takes
        // it: the daemon has no screen for the placeholder (F729).
        if (onPlaceholderOutput(w) || !m_effect->shouldHandleWindow(w, nullptr, /*exemptFullscreen=*/true)) {
            continue;
        }
        const WindowSnapshot now = snapshotOf(w);
        if (now.outputUuid.isEmpty() || now.screenId.isEmpty()) {
            continue;
        }
        const WindowSnapshot base = baseIt != m_settleBaseline.constEnd() ? *baseIt : now;
        bool report = !(now == base);

        // S0: the record for the output it is on, else one not classified
        // yet, else any.
        const EvacueeRecord* s0 = nullptr;
        if (hasRecords) {
            for (auto it = recordsIt->cbegin(); it != recordsIt->cend(); ++it) {
                if (!s0 || (s0->classified && !it->classified)) {
                    s0 = &it.value();
                }
                // A record naming an output that is connected again is due a
                // verdict even when KWin moved nothing this time (F728: an
                // MST dock returns its outputs over several settles).
                report = report || outputConnected(it.key());
            }
            if (const auto onOutput = recordsIt->constFind(now.outputUuid); onOutput != recordsIt->cend()) {
                s0 = &onOutput.value();
            }
        }
        if (!report) {
            continue;
        }
        OutputSettleRow row;
        row.windowId = m_effect->getWindowId(w);
        row.outputUuid = now.outputUuid;
        row.screenId = now.screenId;
        row.sourceScreenId = base.screenId;
        row.sourceConnected = outputConnected(base.outputUuid);
        row.x = now.frame.x();
        row.y = now.frame.y();
        row.width = now.frame.width();
        row.height = now.frame.height();
        row.baseX = base.frame.x();
        row.baseY = base.frame.y();
        row.baseWidth = base.frame.width();
        row.baseHeight = base.frame.height();
        row.moveResizeCount = now.moveResizeCount;
        row.maximizeMode = now.maximizeMode;
        row.quickTileMode = now.quickTileMode;
        row.fullscreen = now.fullscreen;
        row.kwinOnly = now.moveResizeCount == base.moveResizeCount && row.windowId != dragged;
        row.placeableNow = !w->isMinimized() && w->isOnCurrentDesktop() && w->isOnCurrentActivity();
        if (s0) {
            row.hasS0 = true;
            row.s0Uuid = s0->state.outputUuid;
            row.s0ScreenId = s0->state.screenId;
            row.s0MoveResizeCount = s0->state.moveResizeCount;
            row.s0MaximizeMode = s0->state.maximizeMode;
            row.s0QuickTileMode = s0->state.quickTileMode;
            row.s0Fullscreen = s0->state.fullscreen;
            row.s0X = s0->state.frame.x();
            row.s0Y = s0->state.frame.y();
            row.s0Width = s0->state.frame.width();
            row.s0Height = s0->state.frame.height();
        }
        rows.append(row);
    }
    return rows;
}

void ScreenChangeHandler::sendSettleReport()
{
    const PhosphorProtocol::OutputSettleRowList rows = buildSettleRows();
    m_settleBaseline.clear();
    m_outputSetChanged = false;
    DeferredCrossings crossings;
    crossings.reserve(m_deferredCrossings.size());
    for (auto it = m_deferredCrossings.cbegin(); it != m_deferredCrossings.cend(); ++it) {
        crossings.append({QPointer<KWin::EffectWindow>(it.key()), it.value()});
    }
    m_deferredCrossings.clear();
    QList<QPointer<KWin::EffectWindow>> skipped;
    skipped.reserve(m_skippedAnnounces.size());
    for (KWin::EffectWindow* w : std::as_const(m_skippedAnnounces)) {
        skipped.append(QPointer<KWin::EffectWindow>(w));
    }
    m_skippedAnnounces.clear();
    if (rows.isEmpty() && crossings.isEmpty() && skipped.isEmpty()) {
        return;
    }
    // No daemon: nothing is parked, so every crossing is a move. An old
    // daemon answers with an error and takes the same path.
    if (!m_effect->isDaemonReady("report an output settle")) {
        applySettleVerdicts({}, crossings, skipped);
        return;
    }
    qCInfo(lcScreenChange) << "Settle: reporting" << rows.size() << "window(s) and" << crossings.size()
                           << "deferred crossing(s)";
    // Sent even with no rows when crossings wait: the reply orders their
    // replay after the virtual-screen configs requested just before this, so
    // a crossing inside a split monitor resolves against the new layout.
    m_settleInFlight = true;
    QDBusPendingCall call =
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("reportOutputSettle"), {QVariant::fromValue(rows)});
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    QPointer<ScreenChangeHandler> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [self, crossings, skipped](QDBusPendingCallWatcher* pending) {
                pending->deleteLater();
                if (!self || self->m_stopped) {
                    return;
                }
                self->m_settleInFlight = false;
                QDBusPendingReply<PhosphorProtocol::OutputSettleVerdictList> reply = *pending;
                if (!reply.isValid()) {
                    qCWarning(lcScreenChange) << "reportOutputSettle failed:" << reply.error().message()
                                              << "- replaying every crossing as a move";
                }
                self->applySettleVerdicts(reply.isValid() ? reply.value() : PhosphorProtocol::OutputSettleVerdictList{},
                                          crossings, skipped);
                // Crossings deferred while the report was in flight.
                self->replayDeferredCrossings();
            });
}

void ScreenChangeHandler::applySettleVerdicts(const PhosphorProtocol::OutputSettleVerdictList& verdicts,
                                              const DeferredCrossings& crossings,
                                              const QList<QPointer<KWin::EffectWindow>>& skipped)
{
    // Every record has had its settle; a returning output resets its own.
    for (QHash<QString, EvacueeRecord>& records : m_evacueeRecords) {
        for (EvacueeRecord& entry : records) {
            entry.classified = true;
        }
    }
    QSet<KWin::EffectWindow*> placed;
    for (const OutputSettleVerdict& verdict : verdicts) {
        if (!verdict.validationError().isEmpty()) {
            continue;
        }
        KWin::EffectWindow* const w = m_effect->findWindowByIdExact(verdict.windowId);
        if (!w || w->isDeleted()) {
            continue;
        }
        switch (verdict.verdict) {
        case OutputSettleVerdict::Readopt:
            applyReadopt(w, verdict.screenId);
            placed.insert(w);
            break;
        case OutputSettleVerdict::EvacueeFloat:
            applyEvacuee(w, verdict.screenId);
            placed.insert(w);
            break;
        case OutputSettleVerdict::Reassert:
            // The daemon is putting it back on its engine's screen: when it
            // lands there it has not crossed anything.
            if (!verdict.screenId.isEmpty()) {
                m_effect->m_trackedScreenPerWindow[w] = verdict.screenId;
                m_effect->m_tilingHandler->updateNotifiedScreen(verdict.windowId, verdict.screenId);
            }
            placed.insert(w);
            break;
        default:
            break;
        }
        if (verdict.verdict != OutputSettleVerdict::EvacueeFloat) {
            // Classified and not parked: a record naming an output that is
            // back has nothing left to wait for. One naming an output still
            // away stays for that output's return.
            const auto it = m_evacueeRecords.find(w);
            if (it != m_evacueeRecords.end()) {
                it->removeIf([](QHash<QString, EvacueeRecord>::iterator entry) {
                    return outputConnected(entry.key());
                });
                if (it->isEmpty()) {
                    m_evacueeRecords.erase(it);
                }
            }
        }
        qCInfo(lcScreenChange) << "Settle verdict for" << verdict.windowId << ":" << verdict.verdict << "on"
                               << verdict.screenId;
    }
    for (const auto& [w, oldScreenId] : crossings) {
        if (!w || w->isDeleted() || placed.contains(w.data())) {
            continue;
        }
        // Still on the placeholder: its crossing resolves once a real output
        // takes it.
        if (onPlaceholderOutput(w)) {
            if (!m_deferredCrossings.contains(w.data())) {
                m_deferredCrossings.insert(w.data(), oldScreenId);
            }
            continue;
        }
        replayCrossing(w, oldScreenId);
    }
    for (const QPointer<KWin::EffectWindow>& w : skipped) {
        if (!w || w->isDeleted() || placed.contains(w.data())) {
            continue;
        }
        m_effect->m_tilingHandler->notifyWindowAdded(w, /*knownFreeFloating=*/false, /*focusEligible=*/false);
    }
}

void ScreenChangeHandler::replayDeferredCrossings()
{
    if (isScreenChangeInProgress() || m_deferredCrossings.isEmpty()) {
        return;
    }
    const QHash<KWin::EffectWindow*, QString> crossings = std::exchange(m_deferredCrossings, {});
    for (auto it = crossings.cbegin(); it != crossings.cend(); ++it) {
        KWin::EffectWindow* const w = it.key();
        if (onPlaceholderOutput(w)) {
            m_deferredCrossings.insert(w, it.value());
            continue;
        }
        replayCrossing(w, it.value());
    }
}

void ScreenChangeHandler::replayCrossing(KWin::EffectWindow* w, const QString& oldScreenId)
{
    if (!w || w->isDeleted()) {
        return;
    }
    // Where it is NOW, against the virtual screens as they are after the
    // change: a window KWin moved and moved back has not crossed.
    const QString current = m_effect->pendingWindowScreenId(w);
    if (current.isEmpty() || current == oldScreenId) {
        return;
    }
    m_effect->m_trackedScreenPerWindow[w] = current;
    qCInfo(lcScreenChange) << "Replaying the crossing of" << m_effect->getWindowId(w) << "from" << oldScreenId << "to"
                           << current;
    if (VirtualScreenId::samePhysical(oldScreenId, current)) {
        applyVirtualScreenCrossing(w, oldScreenId, current);
    } else {
        applyOutputCrossing(w, oldScreenId, current);
    }
}

void ScreenChangeHandler::applyEvacuee(KWin::EffectWindow* w, const QString& screenId)
{
    const QString windowId = m_effect->getWindowId(w);
    // The daemon floated it in place and B's tiles stay where they are. Here
    // the effect lets go of what it held for it, without handing KWin's
    // maximize or fullscreen back (evacuee_keeps_kwin_state).
    m_effect->m_snapHandler->clearWindowSnapped(windowId);
    // A parked strip column KWin left off every output is brought onto the
    // one it is counted on, at its size (F644).
    const QRect frame = w->frameGeometry().toRect();
    const bool onAnOutput =
        std::any_of(KWin::effects->screens().cbegin(), KWin::effects->screens().cend(), [&frame](const auto* output) {
            return output && QRect(output->geometry()).intersects(frame);
        });
    if (!onAnOutput) {
        if (KWin::LogicalOutput* const output = m_effect->windowOutput(w)) {
            const QRect area = KWin::effects->clientArea(KWin::MaximizeArea, output).toRect();
            QRect target(QPoint(), frame.size().boundedTo(area.size()));
            target.moveCenter(area.center());
            m_effect->applyWindowGeometry(w, target, /*allowDuringDrag=*/false, /*skipAnimation=*/true);
        }
    }
    const QString current = screenId.isEmpty() ? m_effect->getWindowScreenId(w) : screenId;
    m_effect->m_tilingHandler->adoptEvacuee(w, current);
    m_effect->m_trackedScreenPerWindow[w] = m_effect->pendingWindowScreenId(w);
    m_effect->invalidateRuleCacheForStateChange(windowId);
}

void ScreenChangeHandler::applyReadopt(KWin::EffectWindow* w, const QString& screenId)
{
    const QString windowId = m_effect->getWindowId(w);
    const QString current = screenId.isEmpty() ? m_effect->getWindowScreenId(w) : screenId;
    m_effect->m_tilingHandler->readoptEvacuee(w, current);
    m_effect->m_trackedScreenPerWindow[w] = current;
    m_effect->invalidateRuleCacheForStateChange(windowId);
}

void ScreenChangeHandler::applyOutputCrossing(KWin::EffectWindow* w, const QString& oldScreenId,
                                              const QString& newScreenId)
{
    if (!w || w->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    // The adopt below reads the exclusion verdict, and the queued
    // invalidation further down lands after it.
    m_effect->evictExclusionVerdicts(windowId);
    // Tiling transfer (autotile to autotile, autotile to snapping, and so
    // on). Runs even mid-drag so the engine drops the window from the old
    // screen's state at once.
    const bool ownCrossing = m_effect->m_tilingHandler->handleWindowOutputChanged(w);
    if (oldScreenId.isEmpty() || oldScreenId == newScreenId) {
        return;
    }
    // ScreenId, ScreenOrientation and the screen's active layout are all
    // per-screen rule inputs, and the verdict cache is keyed on (windowId,
    // rule-set revision), neither of which moves here. Mid-drag the
    // invalidation waits for callEndDrag, which drains it once the daemon's
    // outcome has landed: nothing at drag end could rediscover the crossing,
    // because the tracked screen already names the new one.
    const bool dragging = m_effect->m_dragTracker->isDragging();
    if (dragging) {
        m_effect->m_dragSuppressedRuleInvalidations.insert(windowId);
    } else {
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
    // The daemon drops what it held of the window on the screen left. A
    // window KWin moved off an output that went away, or back onto one that
    // returned, never reaches here: the settle classified it. Not mid-drag,
    // where the drop owns the transitions.
    if (ownCrossing && !dragging) {
        reportCrossing(w, oldScreenId, newScreenId);
    }
}

void ScreenChangeHandler::reportCrossing(KWin::EffectWindow* w, const QString& oldScreenId, const QString& newScreenId)
{
    if (!w || oldScreenId.isEmpty() || newScreenId.isEmpty() || oldScreenId == newScreenId) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    TilingHandler* const tiling = m_effect->m_tilingHandler.get();
    if (!tiling->isManagedScreen(oldScreenId) && !tiling->isManagedScreen(newScreenId)) {
        // Snap memory decides: a placement of the daemon's own (a restore or
        // resnap that stored the new screen already) keeps its zone.
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("windowScreenChanged"), {windowId, newScreenId},
                                                       QStringLiteral("cross-screen move"));
        return;
    }
    // A tiling engine runs an end: the effect already handed the window over,
    // and the daemon drops every hold left on the screen it came from (F401,
    // F446, F470).
    PhosphorProtocol::ClientHelpers::fireAndForget(
        m_effect, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("windowCrossedScreens"),
        {windowId, oldScreenId, newScreenId}, QStringLiteral("cross-screen move"));
}

void ScreenChangeHandler::applyVirtualScreenCrossing(KWin::EffectWindow* w, const QString& oldScreenId,
                                                     const QString& newScreenId)
{
    if (!w || w->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    m_effect->evictExclusionVerdicts(windowId);
    // The same rule-verdict staleness as an output crossing, and it runs ahead
    // of the delegation below, which returns early for tracked and autotile
    // windows whose verdicts are stale all the same.
    if (m_effect->m_dragTracker->isDragging()) {
        m_effect->m_dragSuppressedRuleInvalidations.insert(windowId);
        // The drag owns the transitions: autotile through the drag-policy
        // path, snapping through the daemon at the drop.
        return;
    }
    m_effect->invalidateRuleCacheForStateChange(windowId);
    // A tracked window's crossing is the tiling handler's own per-frame
    // detector's to handle (it reports too). Only an untracked window
    // (snapping entering an autotile virtual screen) needs the delegation.
    TilingHandler* const tiling = m_effect->m_tilingHandler.get();
    if (tiling->isTrackedWindow(windowId)) {
        return;
    }
    if (tiling->handleWindowOutputChanged(w)) {
        reportCrossing(w, oldScreenId, newScreenId);
    }
}

void ScreenChangeHandler::applyGestureEndCrossing(KWin::EffectWindow* w, const QString& oldScreenId,
                                                  const QString& newScreenId)
{
    if (!w || w->isDeleted()) {
        return;
    }
    if (isScreenChangeInProgress()) {
        deferCrossing(w, oldScreenId);
        return;
    }
    if (!PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing(oldScreenId, newScreenId)) {
        applyOutputCrossing(w, oldScreenId, newScreenId);
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    TilingHandler* const tiling = m_effect->m_tilingHandler.get();
    if (!tiling->isTrackedWindow(windowId)) {
        applyVirtualScreenCrossing(w, oldScreenId, newScreenId);
        return;
    }
    m_effect->evictExclusionVerdicts(windowId);
    m_effect->invalidateRuleCacheForStateChange(windowId);
    if (tiling->handleWindowOutputChanged(w)) {
        reportCrossing(w, oldScreenId, newScreenId);
    }
}

} // namespace PlasmaZones
