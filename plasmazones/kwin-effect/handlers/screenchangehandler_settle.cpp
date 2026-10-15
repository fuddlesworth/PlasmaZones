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
#include "plasmazoneseffect/desktopvisibility.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "plasmazoneseffect/windowcontexthold.h"

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
#include <QScopeGuard>

#include <algorithm>
#include <utility>

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

/// Where KWin has put the window, committed or not (see snapshotOf).
QString moveResizeOutputUuid(KWin::EffectWindow* w)
{
    const KWin::Window* const kw = w ? w->window() : nullptr;
    return kw && kw->moveResizeOutput() ? kw->moveResizeOutput()->uuid() : QString();
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
    if (!w || oldScreenId.isEmpty()) {
        return;
    }
    // The dragged window's crossing waits for its drop whether or not a screen
    // change is running, and settles by what the drop did (settleDragCrossing).
    // A replay after the drop would run the crossing as if nothing answered it
    // (F1075). Held as applyOutputCrossing holds it, rule verdicts included.
    if (m_effect->m_dragTracker->isDragging() && m_effect->m_dragTracker->draggedWindow() == w) {
        if (!m_heldDragCrossings.contains(w)) {
            m_heldDragCrossings.insert(w, oldScreenId);
        }
        const QString windowId = m_effect->getWindowId(w);
        m_effect->evictExclusionVerdicts(windowId);
        m_effect->m_dragSuppressedRuleInvalidations.insert(windowId);
        return;
    }
    if (m_deferredCrossings.contains(w)) {
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
    m_settleCrossings.remove(w);
    m_skippedAnnounces.remove(w);
    m_heldDragCrossings.remove(w);
    m_placeholderOpens.remove(w);
    m_cascadeOutputs.remove(w);
    m_movedOutsideCascade.remove(w);
    m_settleParkDrops.remove(w);
}

void ScreenChangeHandler::deferPlaceholderOpen(KWin::EffectWindow* w)
{
    if (!w || m_stopped) {
        return;
    }
    m_placeholderOpens.insert(w);
    qCInfo(lcScreenChange) << "Window" << m_effect->getWindowId(w)
                           << "opened on KWin's placeholder output; it is placed once a real output takes it";
}

void ScreenChangeHandler::routePlaceholderOpens()
{
    // Never placed, so a real output taking one is no move: it is routed as
    // the open it is, and its crossing off the placeholder is dropped (F750).
    // One still on the placeholder waits for the next settle.
    const QSet<KWin::EffectWindow*> opens = m_placeholderOpens;
    for (KWin::EffectWindow* const w : opens) {
        if (!w->isDeleted() && onPlaceholderOutput(w)) {
            continue;
        }
        m_placeholderOpens.remove(w);
        m_deferredCrossings.remove(w);
        if (w->isDeleted()) {
            continue;
        }
        releaseSettleHold(w);
        qCInfo(lcScreenChange) << "Routing" << m_effect->getWindowId(w) << "as an open: a real output took it";
        m_effect->m_tilingHandler->deferWindowRouting(w);
    }
}

void ScreenChangeHandler::openOutputCascade()
{
    // KWin moves the windows of an output change inside the one call that
    // emits the signal opening this (Workspace::updateOutputs: the output
    // signals, desktopResized, the placement tracker's restore), so each
    // window's output when that turn ends is KWin's doing. One changed
    // outside such a turn was not: a Move to Screen from the keyboard or the
    // window menu advances no interactive move count, and reported as KWin's
    // the daemon re-asserts the placement the window left and moves it back
    // (F1009). Sampled at the turn's edges rather than per outputChanged: a
    // window's moveResizeOutput follows a commanded move at once, while the
    // outputChanged of a Wayland configure lands at the client's ack.
    if (m_cascadeOpen) {
        return;
    }
    m_cascadeOpen = true;
    for (auto it = m_cascadeOutputs.cbegin(); it != m_cascadeOutputs.cend(); ++it) {
        if (moveResizeOutputUuid(it.key()) != it.value()) {
            m_movedOutsideCascade.insert(it.key());
        }
    }
    QTimer::singleShot(0, this, [this]() {
        m_cascadeOpen = false;
        if (m_stopped) {
            return;
        }
        for (KWin::EffectWindow* w : KWin::effects->stackingOrder()) {
            if (w && !w->isDeleted() && m_effect->shouldHandleWindow(w, nullptr, /*exemptFullscreen=*/true)) {
                m_cascadeOutputs.insert(w, moveResizeOutputUuid(w));
            }
        }
    });
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
    m_settleParkDrops.clear();
    m_reseatedPark.reset();
}

bool ScreenChangeHandler::arrivalOn(KWin::EffectWindow* w, const QString& physicalId) const
{
    const TilingHandler* const tiling = m_effect->m_tilingHandler.get();
    const QString windowId = m_effect->getWindowId(w);
    return tiling->announceInFlight(windowId)
        && VirtualScreenId::samePhysical(tiling->notifiedScreenFor(windowId), physicalId);
}

void ScreenChangeHandler::slotParkReseated(const QString& windowId, const QString& outputUuid)
{
    m_reseatedPark.reset();
    KWin::EffectWindow* const w = m_effect->findWindowByIdExact(windowId);
    if (!w) {
        return;
    }
    // Inside a settle the Readopt verdict gives back what the seat restored.
    // An arrival re-seat (readoptOnArrival, inside the window's announce on
    // that output) has no verdict: what the verdict runs runs here, at the
    // seat, whether or not the daemon keeps the entry parked for a context
    // granted there (F1147). The output is the record's, or the announced
    // screen's when no record names the uuid.
    TilingHandler* const tiling = m_effect->m_tilingHandler.get();
    const QString liveId = m_effect->getWindowId(w);
    const QString announced = tiling->notifiedScreenFor(liveId);
    QString physicalId = announced;
    if (const auto records = m_evacueeRecords.constFind(w); records != m_evacueeRecords.cend()) {
        if (const auto record = records->constFind(outputUuid); record != records->cend()) {
            physicalId = record->state.screenId;
        }
    }
    if ((!outputUuid.isEmpty() && !outputConnected(outputUuid)) || !arrivalOn(w, physicalId)) {
        return;
    }
    qCDebug(lcScreenChange) << "Park re-seat for" << windowId << "on" << physicalId
                            << "inside its announce there: the arrival re-seat";
    applyReadopt(w, announced);
    // The parkDropped that follows when nothing is left parked there reads
    // this, within this announce only (its stamp).
    m_reseatedPark = ReseatMark{windowId, outputUuid, tiling->announceGeneration(liveId)};
}

void ScreenChangeHandler::slotParkDropped(const QString& windowId, const QString& outputUuid)
{
    // Consumed on every path. A seat whose entry stayed parked sends no drop,
    // so its mark is also held to the announce it came in (F1147).
    KWin::EffectWindow* const w = m_effect->findWindowByIdExact(windowId);
    const bool reseated = w && m_reseatedPark && m_reseatedPark->windowId == windowId
        && m_reseatedPark->outputUuid == outputUuid
        && m_reseatedPark->announce == m_effect->m_tilingHandler->announceGeneration(m_effect->getWindowId(w));
    m_reseatedPark.reset();
    const auto it = w ? m_evacueeRecords.find(w) : m_evacueeRecords.end();
    if (it == m_evacueeRecords.end()) {
        return;
    }
    // No uuid: the daemon's ledger entry takes its output's uuid only from a
    // settle row's S0 (reportOutputSettle), so a park dropped with none is
    // for an output no answered row carried as S0: a touch before the first
    // settle was answered, or the record a row did not carry when a window
    // had several. Only records never sent as S0 can name it. One that was
    // sent names a park the daemon drops with its uuid, or one it still
    // holds, whose Readopt must still find its entries (F1141).
    if (outputUuid.isEmpty()) {
        for (auto entry = it->begin(); entry != it->end();) {
            if (entry->sentAsS0) {
                ++entry;
                continue;
            }
            forgetParkEntries(w, entry->state.screenId, entry.key(), reseated);
            entry = it->erase(entry);
        }
        if (it->isEmpty()) {
            m_evacueeRecords.erase(it);
        }
        return;
    }
    if (const auto entry = it->constFind(outputUuid); entry != it->cend()) {
        forgetParkEntries(w, entry->state.screenId, outputUuid, reseated);
    }
    it->remove(outputUuid);
    if (it->isEmpty()) {
        m_evacueeRecords.erase(it);
    }
}

void ScreenChangeHandler::forgetParkEntries(KWin::EffectWindow* w, const QString& physicalId, const QString& uuid,
                                            bool reseated)
{
    TilingHandler* const tiling = m_effect->m_tilingHandler.get();
    const QString windowId = m_effect->getWindowId(w);
    // A park for an output still away waited for a Readopt that will not come
    // now (F1128). A connected output's park goes in a Readopt just before it
    // answers: inside the window's own announce on that output (the arrival
    // re-seat, readoptOnArrival), or inside a settle, whose reply carries the
    // verdict. Any other drop for a connected output is a touch where KWin did
    // not pull the window back, with no Readopt to come (F1139).
    if (outputConnected(uuid)) {
        if (arrivalOn(w, physicalId)) {
            // A Readopt the effect gets no verdict for. One that seated the
            // window gave the minimize-float back at its parkReseated just
            // ahead, and the claim waits for the re-seat batch. One that
            // seated nothing left the window to a plain announce, where a
            // rule may float it, and its entries go (F1147).
            qCDebug(lcScreenChange) << "Park drop for" << windowId << "on" << physicalId
                                    << "inside its announce there: arrival re-seat, seated:" << reseated;
            if (!reseated) {
                tiling->forgetEvacueeParksOn(windowId, physicalId);
            }
            return;
        }
        if (m_settleInFlight) {
            qCDebug(lcScreenChange) << "Park drop for" << windowId << "on" << physicalId
                                    << "inside the settle: its verdict decides";
            m_settleParkDrops[w].insert(VirtualScreenId::extractPhysicalId(physicalId));
            return;
        }
    }
    tiling->forgetEvacueeParksOn(windowId, physicalId);
}

PhosphorProtocol::OutputSettleRowList ScreenChangeHandler::buildSettleRows() const
{
    PhosphorProtocol::OutputSettleRowList rows;
    // A resolution or arrangement change alone moves no window between
    // outputs, and its crossings replay as moves without a verdict.
    if (!m_outputSetChanged) {
        return rows;
    }
    const QString dragged =
        m_effect->m_dragTracker->isDragging() ? m_effect->m_dragTracker->draggedWindowId() : QString();
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
        // it: the daemon has no screen for the placeholder (F729). One that
        // opened there was never placed, and is routed as an open (F750).
        if (onPlaceholderOutput(w) || m_placeholderOpens.contains(w)
            || !m_effect->shouldHandleWindow(w, nullptr, /*exemptFullscreen=*/true)) {
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
        const auto cascadeOutput = m_cascadeOutputs.constFind(w);
        row.kwinOnly = GestureEndDecisions::settleRowKwinOnly(
            base.moveResizeCount, now.moveResizeCount, row.windowId == dragged, m_movedOutsideCascade.contains(w),
            cascadeOutput != m_cascadeOutputs.cend() ? *cascadeOutput : QString(), now.outputUuid);
        row.placeableNow = !w->isMinimized() && isOnOwnOutputCurrentDesktop(w) && w->isOnCurrentActivity();
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
    m_cascadeOutputs.clear();
    m_movedOutsideCascade.clear();
    m_outputSetChanged = false;
    DeferredCrossings crossings;
    crossings.reserve(m_deferredCrossings.size());
    for (auto it = m_deferredCrossings.cbegin(); it != m_deferredCrossings.cend(); ++it) {
        crossings.append({QPointer<KWin::EffectWindow>(it.key()), it.value()});
        m_settleCrossings.insert(it.key());
    }
    m_deferredCrossings.clear();
    QList<QPointer<KWin::EffectWindow>> skipped;
    skipped.reserve(m_skippedAnnounces.size());
    for (KWin::EffectWindow* w : std::as_const(m_skippedAnnounces)) {
        skipped.append(QPointer<KWin::EffectWindow>(w));
    }
    m_skippedAnnounces.clear();
    if (rows.isEmpty() && crossings.isEmpty() && skipped.isEmpty()) {
        routePlaceholderOpens();
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
    // The records each row carries as its S0, by window id and output uuid,
    // and each row's source screen. The source is read here, not from the
    // tiling handler at the verdict: the virtual-screen refresh requested
    // just before this report answers first, and re-keys that handler's
    // tracked screens to where KWin put each window (F1148).
    QList<QPair<QString, QString>> sentS0;
    QHash<QString, QString> sources;
    for (const OutputSettleRow& row : rows) {
        if (row.hasS0) {
            sentS0.append({row.windowId, row.s0Uuid});
        }
        sources.insert(row.windowId, row.sourceScreenId);
    }
    QDBusPendingCall call =
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("reportOutputSettle"), {QVariant::fromValue(rows)});
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    QPointer<ScreenChangeHandler> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [self, crossings, skipped, sentS0, sources](QDBusPendingCallWatcher* pending) {
                pending->deleteLater();
                if (!self || self->m_stopped) {
                    return;
                }
                self->m_settleInFlight = false;
                QDBusPendingReply<PhosphorProtocol::OutputSettleVerdictList> reply = *pending;
                if (!reply.isValid()) {
                    qCWarning(lcScreenChange) << "reportOutputSettle failed:" << reply.error().message()
                                              << "- replaying every crossing as a move";
                } else {
                    // Answered: the daemon's entry for each output carried has its uuid now.
                    for (const auto& [windowId, uuid] : sentS0) {
                        KWin::EffectWindow* const w = self->m_effect->findWindowByIdExact(windowId);
                        const auto records = w ? self->m_evacueeRecords.find(w) : self->m_evacueeRecords.end();
                        if (records == self->m_evacueeRecords.end()) {
                            continue;
                        }
                        if (const auto record = records->find(uuid); record != records->end()) {
                            record->sentAsS0 = true;
                        }
                    }
                }
                self->applySettleVerdicts(reply.isValid() ? reply.value() : PhosphorProtocol::OutputSettleVerdictList{},
                                          crossings, skipped, sources);
                // Crossings deferred while the report was in flight.
                self->replayDeferredCrossings();
            });
}

void ScreenChangeHandler::applySettleVerdicts(const PhosphorProtocol::OutputSettleVerdictList& verdicts,
                                              const DeferredCrossings& crossings,
                                              const QList<QPointer<KWin::EffectWindow>>& skipped,
                                              const QHash<QString, QString>& sources)
{
    // Every record has had its settle; a returning output resets its own.
    for (QHash<QString, EvacueeRecord>& records : m_evacueeRecords) {
        for (EvacueeRecord& entry : records) {
            entry.classified = true;
        }
    }
    QSet<KWin::EffectWindow*> placed;
    QHash<KWin::EffectWindow*, QString> readopted;
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
            readopted.insert(w, verdict.screenId.isEmpty() ? m_effect->getWindowScreenId(w) : verdict.screenId);
            break;
        case OutputSettleVerdict::EvacueeFloat:
            applyEvacuee(w, verdict.screenId, sources.value(verdict.windowId));
            placed.insert(w);
            break;
        case OutputSettleVerdict::Reassert:
            // The daemon is putting it back on its engine's screen: when it
            // lands there it has not crossed anything.
            if (!verdict.screenId.isEmpty()) {
                m_effect->m_trackedScreenPerWindow[w] = verdict.screenId;
                m_effect->m_tilingHandler->updateNotifiedScreen(verdict.windowId, verdict.screenId);
            }
            m_effect->m_tilingHandler->forgetEvacueeParks(verdict.windowId);
            placed.insert(w);
            break;
        case OutputSettleVerdict::UserMove:
            // Touched: the daemon dropped its parks (F1120).
            m_effect->m_tilingHandler->forgetEvacueeParks(verdict.windowId);
            break;
        default:
            break;
        }
        // A window dragged while its output went away is placed by its verdict
        // (an EvacueeFloat, since the drag is a touch), which answers its move
        // off that output. Its held crossing now starts where the verdict left
        // it, so the drop settles only the user's own move from there, never a
        // replay from the dead screen (F1112). Only a crossing held from a
        // screen that went away: one held from a live screen is a move the
        // verdict did not answer, and the drop still reports it, so that
        // screen's engine lets the window go (F1129).
        if (const auto held = m_heldDragCrossings.find(w);
            held != m_heldDragCrossings.end() && placed.contains(w) && !m_effect->outputForScreenId(*held)) {
            *held = verdict.screenId.isEmpty() ? m_effect->pendingWindowScreenId(w) : verdict.screenId;
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
    // A connected output's park dropped while this report was out: its
    // Readopt keeps the entries for the re-seat, and with none they go
    // (F1139).
    const QHash<KWin::EffectWindow*, QSet<QString>> parkDrops = std::exchange(m_settleParkDrops, {});
    for (auto it = parkDrops.cbegin(); it != parkDrops.cend(); ++it) {
        const QString returned = readopted.value(it.key());
        const QString windowId = m_effect->getWindowId(it.key());
        for (const QString& physicalId : it.value()) {
            if (!returned.isEmpty() && VirtualScreenId::samePhysical(returned, physicalId)) {
                continue;
            }
            m_effect->m_tilingHandler->forgetEvacueeParksOn(windowId, physicalId);
        }
    }
    for (const auto& [w, oldScreenId] : crossings) {
        m_settleCrossings.remove(w.data());
        if (!w || w->isDeleted() || placed.contains(w.data())) {
            continue;
        }
        // Still on the placeholder: its crossing resolves once a real output
        // takes it. A screen change that began while the report was out is
        // moving the outputs again, so the crossing waits for that settle, as
        // replayDeferredCrossings makes one wait (F1077). It crossed before any
        // crossing deferred since, so its old screen is the one kept.
        if (onPlaceholderOutput(w) || m_pendingScreenChange) {
            m_deferredCrossings.insert(w.data(), oldScreenId);
            continue;
        }
        replayCrossing(w, oldScreenId);
    }
    m_settleCrossings.clear();
    // A placed window's verdict is its move's answer, so its held edits run
    // now, against where the verdict left it.
    for (KWin::EffectWindow* const w : std::as_const(placed)) {
        releaseSettleHold(w);
    }
    for (const QPointer<KWin::EffectWindow>& w : skipped) {
        if (!w || w->isDeleted() || placed.contains(w.data())) {
            continue;
        }
        m_effect->m_tilingHandler->notifyWindowAdded(w, /*knownFreeFloating=*/false, /*focusEligible=*/false);
    }
    routePlaceholderOpens();
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
    // The edits held for the crossing (EE) run once it is resolved, after its
    // notice, crossed or not.
    const auto settled = qScopeGuard([this, w]() {
        releaseSettleHold(w);
    });
    // An open waiting for a real output crossed nothing: it is routed (F750).
    if (m_placeholderOpens.contains(w)) {
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

void ScreenChangeHandler::releaseSettleHold(KWin::EffectWindow* w)
{
    // A window that crossed again meanwhile keeps waiting for that crossing.
    if (w && !crossingAwaitsSettle(w)) {
        m_effect->releaseContextHold(w, WindowContextHold::SettlePending);
    }
}

void ScreenChangeHandler::applyEvacuee(KWin::EffectWindow* w, const QString& screenId, const QString& sourceScreenId)
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
    // The outputs whose park the daemon still holds. One it dropped in this
    // settle (a touch) lost its record to the parkDropped ahead of the
    // verdict, and keeps nothing for a return (F1128).
    QSet<QString> parkedOutputs;
    for (const EvacueeRecord& record : m_evacueeRecords.value(w)) {
        parkedOutputs.insert(VirtualScreenId::extractPhysicalId(record.state.screenId));
    }
    m_effect->m_tilingHandler->adoptEvacuee(w, current, sourceScreenId, parkedOutputs);
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
    // Only the dragged window's crossing waits for the drop, which owns that
    // window's transitions. Any other window crossing during a drag is its
    // own move, and nothing at drag end would rediscover it (F241).
    const bool dragging = m_effect->m_dragTracker->isDragging();
    const bool dragSubject = dragging && m_effect->m_dragTracker->draggedWindow() == w;
    // Tiling transfer (autotile to autotile, autotile to snapping, and so
    // on). Not for the dragged window: its crossing is held and settled once
    // at the drop, by what the drop did, so the tiling monitor it passes over
    // is not rearranged under the pointer (settleDragCrossing, F713, F731).
    bool ownCrossing = false;
    if (dragSubject) {
        if (!m_heldDragCrossings.contains(w)) {
            m_heldDragCrossings.insert(w, oldScreenId);
        }
    } else {
        ownCrossing = m_effect->m_tilingHandler->handleWindowOutputChanged(w);
    }
    if (oldScreenId.isEmpty() || oldScreenId == newScreenId) {
        return;
    }
    // ScreenId, ScreenOrientation and the screen's active layout are all
    // per-screen rule inputs, and the verdict cache is keyed on (windowId,
    // rule-set revision), neither of which moves here. Mid-drag the
    // invalidation waits for callEndDrag, which drains it once the daemon's
    // outcome has landed: nothing at drag end could rediscover the crossing,
    // because the tracked screen already names the new one.
    if (dragging) {
        m_effect->m_dragSuppressedRuleInvalidations.insert(windowId);
    } else {
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
    // The daemon drops what it held of the window on the screen left. A
    // window KWin moved off an output that went away, or back onto one that
    // returned, never reaches here: the settle classified it.
    if (ownCrossing) {
        reportCrossing(w, oldScreenId, newScreenId);
    }
}

void ScreenChangeHandler::settleDragCrossing(KWin::EffectWindow* w, GestureEndDecisions::DropAnswer answer)
{
    const auto held = m_heldDragCrossings.constFind(w);
    if (held == m_heldDragCrossings.constEnd()) {
        return;
    }
    const QString startScreenId = held.value();
    m_heldDragCrossings.erase(held);
    if (w->isDeleted()) {
        return;
    }
    const QString landed = m_effect->pendingWindowScreenId(w);
    m_effect->m_trackedScreenPerWindow[w] = landed;
    if (startScreenId.isEmpty() || landed.isEmpty() || startScreenId == landed) {
        return;
    }
    TilingHandler* const tiling = m_effect->m_tilingHandler.get();
    const QString windowId = m_effect->getWindowId(w);
    using GestureEndDecisions::DragCrossingSettle;
    switch (GestureEndDecisions::settleDragCrossing(answer, tiling->isTrackedWindow(windowId),
                                                    tiling->isManagedScreen(landed))) {
    case DragCrossingSettle::ReKeyOnly:
        tiling->updateNotifiedScreen(windowId, landed);
        return;
    case DragCrossingSettle::TrackOnly:
        tiling->handleWindowOutputChanged(w, TilingHandler::ReAdd::TrackOnly);
        return;
    case DragCrossingSettle::RunBody:
        tiling->handleWindowOutputChanged(w);
        return;
    case DragCrossingSettle::RunBodyAndReport:
        if (tiling->handleWindowOutputChanged(w)) {
            reportCrossing(w, startScreenId, landed);
        }
        return;
    }
}

void ScreenChangeHandler::reportCrossing(KWin::EffectWindow* w, const QString& oldScreenId, const QString& newScreenId)
{
    if (!w || oldScreenId.isEmpty() || newScreenId.isEmpty() || oldScreenId == newScreenId) {
        return;
    }
    // Nothing to tell a daemon that is not there: a fresh one learns every
    // window's screen at bring-up (F573).
    if (!m_effect->m_daemonGate.serviceRegistered) {
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
        if (m_effect->m_dragTracker->draggedWindow() == w) {
            // The drag owns the dragged window's transitions: held like the
            // output arm's and settled at the drop (settleDragCrossing, F736).
            // Any other window's crossing is its own (F241).
            if (!m_heldDragCrossings.contains(w)) {
                m_heldDragCrossings.insert(w, oldScreenId);
            }
            return;
        }
    } else {
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
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
