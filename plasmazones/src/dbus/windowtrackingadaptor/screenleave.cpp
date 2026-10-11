// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — leaving a screen
//
// A window is on one screen. What an engine still holds of it on another is
// stale memory: a zone there names another layout, a tile there belongs to
// another strip, and a membership pass, a resnap or a desktop switch on that
// screen would re-apply it and pull the window back across monitors
// (memory_clears_on_move). releaseLeftScreens drops it wherever a window
// changes screens, and windowScreenChanged handles a move the effect reports
// between two screens no tiling engine manages.
// ═══════════════════════════════════════════════════════════════════════════════

#include "windowtrackingadaptor.h"
#include "internal.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorTileEngine/AutotileEngine.h>

namespace PlasmaZones {

namespace WindowTrackingInternal {

/// The screen of a store that holds @p windowId as a member with a zone, on
/// a screen other than @p excludeScreen; empty when there is none.
QString snapZoneScreen(PhosphorSnapEngine::SnapEngine* snap, const QString& windowId, const QString& excludeScreen)
{
    if (!snap) {
        return {};
    }
    for (PhosphorSnapEngine::SnapState* state : snap->allSnapStates()) {
        if (state && snap->holdsWindowInState(windowId, state) && state->isWindowSnapped(windowId)
            && !PhosphorScreens::ScreenIdentity::screensMatch(state->screenId(), excludeScreen)) {
            return state->screenId();
        }
    }
    return {};
}

bool snapHoldsOffScreen(PhosphorSnapEngine::SnapEngine* snap, const QString& windowId, const QString& keepScreenId)
{
    if (!snap) {
        return false;
    }
    for (PhosphorSnapEngine::SnapState* state : snap->allSnapStates()) {
        if (state && snap->holdsWindowInState(windowId, state)
            && !PhosphorScreens::ScreenIdentity::screensMatch(state->screenId(), keepScreenId)) {
            return true;
        }
    }
    return false;
}

} // namespace WindowTrackingInternal

namespace {

/// The effective screen a report of @p reported names for a window whose
/// stored screen is @p stored and whose frame is @p frame. KWin reports the
/// physical output; on a subdivided output the screen to compare against is
/// the virtual screen the window is in. A stored virtual screen on the
/// reported output is still right; otherwise the frame says which virtual
/// screen of the reported output the window landed in.
QString resolveReportedScreen(PhosphorScreens::ScreenManager* mgr, const QString& reported, const QString& stored,
                              const QRect& frame)
{
    if (PhosphorIdentity::VirtualScreenId::isVirtual(reported)) {
        return reported;
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(stored)
        && PhosphorScreens::ScreenIdentity::belongsToPhysicalScreen(stored, reported)) {
        return stored;
    }
    if (mgr && frame.isValid()) {
        const QString vs = Utils::effectiveScreenIdAt(mgr, frame.center());
        if (!vs.isEmpty() && PhosphorScreens::ScreenIdentity::belongsToPhysicalScreen(vs, reported)) {
            return vs;
        }
    }
    return reported;
}

} // namespace

void WindowTrackingAdaptor::releaseLeftScreens(const QString& windowId, const QString& keepScreenId,
                                               const PhosphorEngine::IPlacementEngine* arrival)
{
    if (!m_service || windowId.isEmpty() || keepScreenId.isEmpty()) {
        return;
    }
    PhosphorSnapEngine::SnapEngine* snap = snapEngine();
    const QString canonical = shadowWindowId(windowId);

    // Whether snap memory is left on another screen, decided before anything
    // is released: a membership there, or a live record slot naming a screen
    // of a connected output (one naming an output that went away is the
    // evacuee park's). A virtual screen of the same monitor is another screen.
    bool snapLeft = WindowTrackingInternal::snapHoldsOffScreen(snap, windowId, keepScreenId);
    if (!snapLeft) {
        const auto rec = m_service->placementStore().peekExact(canonical);
        if (rec) {
            const PhosphorEngine::EngineSlot slot = rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
            const bool live = !slot.state.isEmpty() && slot.state != PhosphorEngine::WindowPlacement::stateReleased();
            const QString recordPhysical = PhosphorIdentity::VirtualScreenId::extractPhysicalId(rec->screenId);
            PhosphorScreens::ScreenManager* mgr = m_service->screenManager();
            snapLeft = live && !recordPhysical.isEmpty()
                && !PhosphorScreens::ScreenIdentity::screensMatch(rec->screenId, keepScreenId) && mgr
                && mgr->physicalScreenFor(recordPhysical).isValid();
        }
    }

    // The other tiling engine's hold off the kept screen: a background
    // desktop's tile or column of the same window. Never the arriving engine,
    // which may still be moving the window (an engine arms its move marker
    // before it re-keys the window).
    for (PhosphorEngine::PlacementEngineBase* engine : {m_autotileEngine.data(), m_scrollEngine.data()}) {
        if (engine && engine != arrival) {
            engine->releaseWindowOffScreen(windowId, keepScreenId);
        }
    }
    // Every snap membership off the kept screen goes, silently: a zone the
    // window holds on a screen it left is memory, not a placement anything
    // shows, so there is no zone change to announce.
    if (snap) {
        snap->releaseWindowOffScreen(windowId, keepScreenId);
    }
    const bool snapArrives = snap && arrival == snap;
    if (!snapArrives) {
        // The screenless float bit snap keeps for an untracked window would
        // answer every snap float query on the new screen.
        if (snap && snap->screenForTrackedWindow(windowId).isEmpty() && snap->isFloating(windowId)) {
            snap->setFloating(windowId, false);
        }
        // The record's snap slot names a zone of the monitor the window left,
        // which the next restore or resnap would apply on this one.
        if (snapLeft) {
            m_service->releaseEngineSlot(windowId, PhosphorEngine::WindowPlacement::snapEngineId());
        }
    }
    // A float bit dropped without an edge leaves the last broadcast saying
    // floating (F716): close it out once, where it was broadcast.
    if (m_broadcastFloating.value(canonical) && !m_service->isWindowFloating(windowId)) {
        relayWindowFloatingChanged(windowId, false, keepScreenId);
    }
    // The daemon keeps per-screen memory of its own: the engine orders a
    // mode round trip seeds from (F695).
    if (m_windowLeftScreenHook) {
        m_windowLeftScreenHook(windowId, keepScreenId);
    }
}

void WindowTrackingAdaptor::windowCrossedScreens(const QString& windowId, const QString& fromScreenId,
                                                 const QString& toScreenId)
{
    if (!m_service || !validateWindowId(windowId, QStringLiteral("crossed screens"))) {
        return;
    }
    if (fromScreenId.isEmpty() || toScreenId.isEmpty()) {
        qCWarning(lcDbusWindow) << "windowCrossedScreens: empty screen for" << windowId << fromScreenId << toScreenId;
        return;
    }
    const QString canonical = shadowWindowId(windowId);
    PhosphorScreens::ScreenManager* mgr = m_service->screenManager();
    const QString crossedKey = QLatin1String("crossed:") + canonical;
    m_heldScreenReports.remove(crossedKey);
    const QString resolved =
        resolveReportedScreen(mgr, toScreenId, m_service->screenForWindow(windowId), m_frameGeometry.value(canonical));
    // A crossing onto a screen the daemon does not know yet releases nothing
    // until it does (F81).
    if (mgr && resolveBusScreen(resolved).isEmpty()) {
        holdScreenReport(crossedKey, resolved, [this, windowId, fromScreenId, toScreenId] {
            windowCrossedScreens(windowId, fromScreenId, toScreenId);
        });
        return;
    }
    // The tiling engine that took the window there, when one did: the effect
    // adopted it on arrival or transferred it before this notice.
    PhosphorEngine::PlacementEngineBase* arrival = nullptr;
    for (PhosphorEngine::PlacementEngineBase* engine : {m_autotileEngine.data(), m_scrollEngine.data()}) {
        if (engine && PhosphorScreens::ScreenIdentity::screensMatch(engine->heldScreenForWindow(windowId), resolved)) {
            arrival = engine;
            break;
        }
    }
    PhosphorSnapEngine::SnapEngine* snap = snapEngine();
    // Decided before anything is released: the effect still shows the window
    // snapped while any store holds it off the new screen.
    const bool heldSnap = WindowTrackingInternal::snapHoldsOffScreen(snap, windowId, resolved)
        || !WindowTrackingInternal::snapZoneScreen(snap, windowId, resolved).isEmpty();
    const auto record = m_service->placementStore().peekExact(canonical);

    releaseLeftScreens(windowId, resolved, arrival);

    // The record names the screen the window left: every engine's slot there
    // goes, the arriving engine's excepted, or a reopen or a mode switch on the
    // new monitor would read a tile or a float of the old one (F673). A record
    // for an output that went away is the evacuee park's.
    if (record) {
        const QString recordPhysical = PhosphorIdentity::VirtualScreenId::extractPhysicalId(record->screenId);
        if (!recordPhysical.isEmpty() && !PhosphorScreens::ScreenIdentity::screensMatch(record->screenId, resolved)
            && mgr && mgr->physicalScreenFor(recordPhysical).isValid()) {
            for (auto it = record->engines.cbegin(); it != record->engines.cend(); ++it) {
                if (!arrival || it.key() != arrival->engineId()) {
                    m_service->releaseEngineSlot(windowId, it.key());
                }
            }
        }
    }
    qCInfo(lcDbusWindow) << "windowCrossedScreens:" << windowId << "from" << fromScreenId << "to" << resolved
                         << "arrival" << (arrival ? arrival->engineId() : QString());
    if (heldSnap) {
        Q_EMIT windowStateChanged(windowId,
                                  PhosphorProtocol::WindowStateEntry{windowId, QString(), resolved, false,
                                                                     QStringLiteral("screen_changed"), QStringList{},
                                                                     false});
    }
}

bool WindowTrackingAdaptor::dragEndedOnScreen(const QString& windowId, const QString& releaseScreenId,
                                              bool floatIfSnapped)
{
    if (!m_service || windowId.isEmpty() || releaseScreenId.isEmpty()) {
        return false;
    }
    // A drop on a screen a tiling engine runs is that engine's drag handling.
    for (PhosphorEngine::PlacementEngineBase* engine : {m_autotileEngine.data(), m_scrollEngine.data()}) {
        if (engine && engine->isActiveOnScreen(releaseScreenId)) {
            return false;
        }
    }
    PhosphorSnapEngine::SnapEngine* snap = snapEngine();
    if (!snap) {
        return false;
    }
    // Only a window snap holds on another screen than the one it was dropped on.
    const QString trackedScreen = snap->screenForTrackedWindow(windowId);
    const QString zoneScreen = WindowTrackingInternal::snapZoneScreen(snap, windowId, releaseScreenId);
    const bool trackedElsewhere =
        !trackedScreen.isEmpty() && !PhosphorScreens::ScreenIdentity::screensMatch(trackedScreen, releaseScreenId);
    if (!trackedElsewhere && zoneScreen.isEmpty()) {
        return false;
    }
    const QString sourceScreen = !zoneScreen.isEmpty() ? zoneScreen : trackedScreen;
    const bool wasSnapped = !WindowTrackingInternal::snapZoneScreen(snap, windowId, QString()).isEmpty();
    // The pre-snap size, read before the release consumes the source screen's
    // memory: the window keeps where it was dropped and gets back its size.
    std::optional<QRect> preSnapGeo;
    if (wasSnapped && shouldRestoreSizeOnUnsnap(windowId)) {
        preSnapGeo = m_service->validatedUnmanagedGeometry(windowId, sourceScreen);
    }
    qCInfo(lcDbusWindow) << "dragEndedOnScreen:" << windowId << "dropped on" << releaseScreenId << "from"
                         << sourceScreen;
    // The move itself: a snapped window loses its zone and everything on the
    // monitor left, a floating one floats on here with no home there.
    windowScreenChanged(windowId, releaseScreenId);
    // A snapped window dragged off without the trigger floats where it was
    // dropped, as it would have on its own screen (F677).
    if (floatIfSnapped && wasSnapped) {
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = releaseScreenId;
        ctx.wasFloating = true;
        ctx.heldFocus = m_lastActiveWindowId == shadowWindowId(windowId);
        ctx.sourceGeometry = m_frameGeometry.value(shadowWindowId(windowId));
        snap->handoffReceive(ctx);
        captureWindowPlacement(windowId, QString(), /*fromStateChange=*/true);
    }
    if (preSnapGeo && preSnapGeo->width() > 0 && preSnapGeo->height() > 0) {
        Q_EMIT applyGeometryRequested(windowId, 0, 0, preSnapGeo->width(), preSnapGeo->height(), QString(),
                                      releaseScreenId, true,
                                      static_cast<int>(PhosphorProtocol::PlacementPurpose::UserVerb));
        m_service->clearFreeGeometry(windowId, sourceScreen);
    }
    return true;
}

void WindowTrackingAdaptor::announceOutputMove(const QString& windowId, const QString& targetScreenId,
                                               const QString& sourceScreenId,
                                               const PhosphorEngine::IPlacementEngine* arrival)
{
    // Every daemon-driven move onto another output leaves the screens it left
    // first, so no move can skip it (hook 1).
    releaseLeftScreens(windowId, targetScreenId, arrival);
    Q_EMIT windowOutputMoveExpected(windowId, targetScreenId, sourceScreenId);
}

void WindowTrackingAdaptor::windowScreenChanged(const QString& windowId, const QString& newScreenId)
{
    if (!m_service)
        return;
    if (!validateWindowId(windowId, QStringLiteral("screen changed"))) {
        return;
    }
    // An empty newScreenId would propagate through the cross-engine
    // handoff below and store an empty toScreenId in the engine's
    // tracking. Bail early — every downstream consumer treats an empty
    // screen id as "no tracking", and re-running with the live screen
    // would arrive via the next windowScreenChanged callback anyway.
    if (newScreenId.isEmpty()) {
        return;
    }
    // The effect reports this only when neither end is managed by a tiling
    // engine, so snap memory decides what happens here and any tiling hold is
    // a background context's.
    const QString screenKey = QLatin1String("screen:") + shadowWindowId(windowId);
    m_heldScreenReports.remove(screenKey);
    const QString resolved =
        resolveReportedScreen(m_service->screenManager(), newScreenId, m_service->screenForWindow(windowId),
                              m_frameGeometry.value(shadowWindowId(windowId)));
    // A move onto a screen the daemon does not know yet changes nothing until
    // it does: the snap leave and the tiling releases below would run against
    // an id nothing can place on (F81).
    if (m_service->screenManager() && resolveBusScreen(resolved).isEmpty()) {
        holdScreenReport(screenKey, resolved, [this, windowId, newScreenId] {
            windowScreenChanged(windowId, newScreenId);
        });
        return;
    }

    // A tiling hold the window keeps on the screen it left (a background
    // desktop's tile or column of a multi-desktop window) is stale memory, on
    // every branch below: returning to that desktop would pull the window back
    // across monitors.
    for (PhosphorEngine::PlacementEngineBase* engine : {m_autotileEngine.data(), m_scrollEngine.data()}) {
        if (engine) {
            engine->releaseWindowOffScreen(windowId, resolved);
        }
    }
    PhosphorSnapEngine::SnapEngine* snap = snapEngine();

    // Floating: refresh the engine's screen tracking through the cross-engine
    // handoff, so shortcut routing finds the new screen and the float toggle
    // does not unfloat the window back on the screen it left. The receive
    // re-homes the window and forgets its pre-float home there.
    if (m_service->zoneForWindow(windowId).isEmpty() && m_service->isWindowFloating(windowId)) {
        const QString trackedSnap = m_snapEngine ? m_snapEngine->screenForTrackedWindow(windowId) : QString();
        const QString trackedAutotile =
            m_autotileEngine ? m_autotileEngine->screenForTrackedWindow(windowId) : QString();
        const QString trackedScroll = m_scrollEngine ? m_scrollEngine->screenForTrackedWindow(windowId) : QString();
        const QString trackedScreen = !trackedSnap.isEmpty() ? trackedSnap
            : !trackedAutotile.isEmpty()                     ? trackedAutotile
                                                             : trackedScroll;
        if (trackedScreen.isEmpty() || PhosphorScreens::ScreenIdentity::screensMatch(trackedScreen, resolved)) {
            return;
        }
        PhosphorEngine::PlacementEngineBase* source = !trackedSnap.isEmpty() ? m_snapEngine.data()
            : !trackedAutotile.isEmpty()                                     ? m_autotileEngine.data()
                                                                             : m_scrollEngine.data();
        PhosphorEngine::PlacementEngineBase* dest = nullptr;
        if (m_autotileEngine && m_autotileEngine->isActiveOnScreen(resolved)) {
            dest = m_autotileEngine.data();
        } else if (m_scrollEngine && m_scrollEngine->isActiveOnScreen(resolved)) {
            dest = m_scrollEngine.data();
        } else if (m_snapEngine) {
            dest = m_snapEngine.data();
        }
        if (!dest) {
            return;
        }
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = resolved;
        ctx.fromEngineId = source ? source->engineId() : QString();
        ctx.wasFloating = true;
        // Canonical-vs-canonical (windowActivated stores the shadow id): the
        // receive seeds its focus memory from this — a screen change that
        // never moved focus produces no report to record the side change.
        ctx.heldFocus = m_lastActiveWindowId == shadowWindowId(windowId);
        ctx.sourceGeometry = m_frameGeometry.value(shadowWindowId(windowId));
        ctx.minSize = source ? source->windowMinimumSize(windowId) : QSize();
        if (WindowTrackingInternal::guardedHandoff(source, dest, ctx, trackedScreen)) {
            qCInfo(lcDbusWindow) << "windowScreenChanged: floating window" << windowId << "moved from" << trackedScreen
                                 << "to" << resolved << "- handoff complete";
            // The record is re-captured now, without the home the receive
            // dropped: an unfloat before the next save would otherwise
            // re-derive it from the record. The float relay dedups on the
            // float bit alone, so the move itself is announced here.
            captureWindowPlacement(windowId, QString(), /*fromStateChange=*/true);
            Q_EMIT windowStateChanged(windowId,
                                      PhosphorProtocol::WindowStateEntry{windowId, QString(), resolved, true,
                                                                         QStringLiteral("screen_changed"),
                                                                         QStringList{}, false});
        }
        return;
    }

    // Snapped in view on the screen it is on: the window was moved there by
    // the daemon itself (restore, resnap, snap assist), so it keeps its snap.
    const QString storedScreen = m_service->screenForWindow(windowId);
    const bool snappedInView = !m_service->zoneForWindow(windowId).isEmpty();
    if (snappedInView && PhosphorScreens::ScreenIdentity::screensMatch(storedScreen, resolved)) {
        qCDebug(lcDbusWindow) << "windowScreenChanged:" << windowId << "moved to assigned screen, keeping snap";
        return;
    }
    // A zone in ANY membership counts, in view or not: a multi-desktop window
    // snapped on a background desktop of the screen it left would be dragged
    // back by that desktop's next membership pass.
    const QString heldScreen =
        snappedInView ? storedScreen : WindowTrackingInternal::snapZoneScreen(snap, windowId, resolved);
    if (heldScreen.isEmpty()) {
        // Free: only residue to release (an adopted membership with no zone,
        // a floating one on another desktop, a pre-float home).
        if (snap) {
            snap->releaseWindowOffScreen(windowId, resolved);
        }
        return;
    }

    qCInfo(lcDbusWindow) << "windowScreenChanged:" << windowId << "moved from" << heldScreen << "to" << resolved
                         << "- unsnapping";
    // The unassign first, in the store holding the zone in view, so the relay
    // subscribers see it go and the last-used naming it clears; then every
    // membership off the new screen goes silently.
    if (snappedInView) {
        m_service->unassignWindow(windowId);
    }
    if (snap) {
        snap->releaseWindowOffScreen(windowId, resolved);
    }
    // The record's snap slot goes too, or the next restore or resnap reads
    // A's zone from it and applies it on B.
    m_service->releaseEngineSlot(windowId, PhosphorEngine::WindowPlacement::snapEngineId());

    // Report the resolved (effective) screen, the same value the decision and
    // log above use, not the raw newScreenId.
    Q_EMIT windowStateChanged(windowId,
                              PhosphorProtocol::WindowStateEntry{windowId, QString(), resolved, false,
                                                                 QStringLiteral("screen_changed"), QStringList{},
                                                                 false});
}

} // namespace PlasmaZones
