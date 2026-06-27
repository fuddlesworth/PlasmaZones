// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snaphandler.h"

#include "autotilehandler.h"
#include "dragtracker.h"
#include "plasmazoneseffect.h"
#include "snapassisthandler.h"

#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/NavigationMarshalling.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>
#include <QSet>
#include <QStringList>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

SnapHandler::SnapHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void SnapHandler::markWindowSnapped(const QString& windowId, const QString& screenId)
{
    // An empty screenId is never a valid snap owner: the per-screen buckets are
    // keyed by screenId, so recording under "" would pollute the set with an
    // entry that the per-screen cross-screen cleanup can never reclaim.
    // Callers route unresolved/float windows through clearWindowSnapped instead;
    // this guard is defensive depth for any path that slips an empty screen in.
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
    if (!w || m_effect->getWindowId(w) != windowId) {
        // Window gone (closed mid-snap — the close often races the async
        // snap reply, so slotWindowClosed's bookkeeping may have ALREADY
        // run). Recording tiled tracking or acquiring decoration ownership
        // now would re-create state nothing will ever clean up; drop any
        // remnants instead. The exact-id check matters: findWindowById's
        // appId fuzzy fallback can resolve a same-app SIBLING for a dead id,
        // and hiding the sibling's title bar under the dead key would be
        // unreleasable.
        AutotileStateHelpers::removeFromAllScreens(m_border, windowId);
        return;
    }
    // A window can only be snap-managed by one screen at a time. Strip stale
    // tiled tracking from any OTHER screen before recording the new owner
    // (mirrors the autotile cross-screen-transfer cleanup in tiling.cpp).
    AutotileStateHelpers::removeFromOtherScreens(m_border, windowId, screenId);
    AutotileStateHelpers::addTiledOnScreen(m_border, screenId, windowId);

    // Title-bar (borderless) state is driven entirely by rules through
    // the effect's reconcileRuleHiddenTitleBar → DecorationManager path; this
    // handler only records snap tiled-tracking for border RENDERING.

    // Border overlays are visual-only, so skip the off-desktop case (consistent
    // with updateAllBorders): an OutlinedBorderItem for an invisible window is
    // wasted work. When the user switches to that window's desktop, the
    // desktopChanged → updateAllBorders connection rebuilds its border.
    if (w->isOnCurrentDesktop()) {
        m_effect->updateWindowBorder(windowId, w);
    }
}

void SnapHandler::clearWindowSnapped(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    AutotileStateHelpers::removeFromAllScreens(m_border, windowId);
    m_effect->removeWindowBorder(windowId);
}

void SnapHandler::clearSnapTracking()
{
    // Bookkeeping only. Physical title-bar restores are the
    // DecorationManager's job — teardown callers pair this with
    // DecorationManager::restoreAll(). Callers also pair it with
    // clearAllBorders() to tear down the OutlinedBorderItem scene items.
    m_border.tiledWindowsByScreen.clear();
}

void SnapHandler::onWindowClosed(const QString& windowId)
{
    // Pure bookkeeping — the window is being destroyed, so no setNoBorder /
    // removeWindowBorder is needed (the border item is removed by the effect's
    // close path and the title bar dies with the window).
    AutotileStateHelpers::removeFromAllScreens(m_border, windowId);
}

void SnapHandler::setFocusFollowsMouse(bool enabled)
{
    m_focusFollowsMouse = enabled;
}

void SnapHandler::callResolveWindowRestore(KWin::EffectWindow* window, std::function<void()> onComplete,
                                           bool releaseSuppressionOnMiss)
{
    if (!window) {
        if (onComplete) {
            onComplete();
        }
        return;
    }

    if (!m_effect->isDaemonReady("resolve window restore")) {
        // No daemon means no snap-restore (and no autotile either — it
        // needs the daemon too). Release first-frame suppression so the
        // window is not held invisible waiting on a reposition that will
        // never come.
        m_effect->endRestoreSuppression(window);
        if (onComplete) {
            onComplete();
        }
        return;
    }

    QPointer<KWin::EffectWindow> safeWindow = window;
    QString windowId = m_effect->getWindowId(window);
    QString screenId = m_effect->getWindowScreenId(window);
    bool sticky = m_effect->isWindowSticky(window);

    // On a resolve miss (daemon found no zone) release first-frame
    // suppression — unless the caller says another path will still
    // reposition the window (autotile-screen path), in which case the
    // suppression must hold until that reposition's geometry settles.
    std::function<void()> onMiss;
    if (releaseSuppressionOnMiss) {
        onMiss = [this, safeWindow]() {
            if (safeWindow) {
                m_effect->endRestoreSuppression(safeWindow);
            }
        };
    }

    // Single D-Bus call — daemon runs the full appRule → persisted → emptyZone → lastZone chain.
    //
    // skipAnimation=true: teleport the window straight into the resolved
    // zone. The animated morph path tweens the window from its spawn
    // position, which both reads as "KDE opened the window, then we moved
    // it" and collides with any in-flight surface-extent window.open
    // shader (bounce / fly-in) — the morph translates the output-spanning
    // shader quad. Placing the window directly lets the open shader play
    // cleanly into the zone.
    //
    // storePreSnap=false: the window is already at its snap/zone position (from before
    // daemon restart or from KWin session restore), so its current frameGeometry is the
    // zone geometry — NOT the free-floating geometry. Storing it as pre-tile would cause
    // float toggle to restore to the zone geometry instead of the original free-floating position.
    const int kindInt = static_cast<int>(m_effect->classifyWindowKind(window));
    m_effect->tryAsyncSnapCall(PhosphorProtocol::Service::Interface::Snap, QStringLiteral("resolveWindowRestore"),
                               {windowId, screenId, sticky, kindInt}, safeWindow, windowId, false, onMiss, nullptr,
                               /*skipAnimation=*/true, onComplete);
}

void SnapHandler::ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId,
                                              const QRectF& preCapturedGeometry)
{
    if (!w || windowId.isEmpty()) {
        return;
    }

    if (!m_effect->isDaemonReady("ensure pre-snap geometry")) {
        return;
    }

    // Use pre-captured geometry if provided, otherwise read from window.
    QRectF geom = preCapturedGeometry.isValid() ? preCapturedGeometry : QRectF(w->frameGeometry());
    if (geom.width() <= 0 || geom.height() <= 0) {
        return;
    }

    // Use virtual-screen-aware ID — getWindowScreenId() falls back to the physical
    // ID when virtual screen defs haven't loaded yet, so it is safe to call
    // unconditionally. Using it here ensures the stored screen ID always matches
    // the ID used by later lookups.
    const QString screenId = m_effect->getWindowScreenId(w);

    // Post the store directly with overwrite=false. The daemon's storePreTileGeometry
    // enforces per-windowId idempotency — a second capture for the same runtime
    // instance is a no-op. We deliberately skip the prior async hasPreTileGeometry
    // pre-check: that path matched on appId too, so a stale cross-session entry from
    // a prior window instance (keyed by appId) would block the fresh per-instance
    // capture and freeze float-restore at ancient coordinates.
    // qRound, not truncation: fractional-scale outputs leave sub-pixel
    // residue in frameGeometry() (same convention as the toRect() geometry
    // paths — see window_lifecycle.cpp).
    PhosphorProtocol::ClientHelpers::fireAndForget(
        m_effect, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
        {windowId, qRound(geom.x()), qRound(geom.y()), qRound(geom.width()), qRound(geom.height()), screenId, false},
        QStringLiteral("storePreTileGeometry"));
    qCInfo(lcEffect) << "Stored pre-tile geometry for window" << windowId << "geom=" << geom;
}

void SnapHandler::handleCursorMoved(const QPointF& pos, const QString& screenId)
{
    if (!m_focusFollowsMouse) {
        return;
    }

    // Pause FFM whenever the active window is NOT snapped into a zone. Any
    // non-snapped active window — a popup/dialog/excluded app, a window the user
    // deliberately floated, or a free window they simply have not snapped — is
    // one the user is working in on top of the snap stack; wandering the cursor
    // over a snapped window beneath it must not steal its focus. FFM resumes
    // (follows the cursor between snapped windows) once a snapped window is
    // active. Scoped to the cursor's screen (mirrors AutotileHandler::
    // handleCursorMoved, discussion #461 + follow-up): a window active on another
    // monitor must not freeze FFM on the monitor the cursor is on. The daemon's
    // own passthrough overlay surface never counts as the kind of active window
    // worth protecting; the interactive editor DOES (it is not a passthrough
    // overlay, so it falls through to the not-snapped pause below and keeps
    // focus). (Autotile pauses on the same principle — floated/popup/under-
    // min-size — but everything else there is tiled, so it has no never-managed
    // "free" case; snap does, hence the single not-snapped predicate.)
    if (KWin::EffectWindow* active = KWin::effects->activeWindow()) {
        // Cheap overlay-class check first, then the heavier screen resolution
        // (mirrors the autotile guard's predicate ordering).
        if (!PlasmaZonesEffect::isOwnPassthroughOverlayClass(active->windowClass())
            && m_effect->getWindowScreenId(active) == screenId && !isTiledWindow(m_effect->getWindowId(active))) {
            return;
        }
    }

    // Find the topmost snapped window under the cursor (stacking order top → bottom).
    const auto windows = KWin::effects->stackingOrder();
    for (int i = windows.size() - 1; i >= 0; --i) {
        KWin::EffectWindow* w = windows[i];
        // isDeleted: a close-grabbed dying window under the cursor must not
        // pause FFM via the occlusion bail (or pollute id caches below).
        if (!w || w->isDeleted() || w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
            continue;
        }
        // Cheap geometry test before the windowClass()/windowId allocations below.
        if (!w->frameGeometry().contains(pos)) {
            continue;
        }
        // Look through the daemon's own passthrough overlay surface — it is
        // full-screen and always topmost, so a bail here would kill FFM whenever
        // an overlay is up (mirrors the autotile FFM guard). The interactive
        // editor is NOT looked through: it falls to the not-snapped bail below,
        // so FFM leaves focus on it instead of stealing to a snapped window.
        if (PlasmaZonesEffect::isOwnPassthroughOverlayClass(w->windowClass())) {
            continue;
        }
        // The window directly under the cursor is not snapped (a floating dialog, popup,
        // or excluded app occluding a snapped window beneath). Don't look through it to
        // focus the snapped window — that would steal focus from what the user is pointing
        // at. Mirrors AutotileHandler::handleCursorMoved's occlusion guard.
        if (!isTiledWindow(m_effect->getWindowId(w))) {
            return;
        }
        if (w == KWin::effects->activeWindow()) {
            return; // Already focused — no-op.
        }
        KWin::effects->activateWindow(w);
        return;
    }
}

void SnapHandler::callCancelSnap()
{
    qCInfo(lcEffect) << "Calling cancelSnap (drag cancelled by Escape or external event)";
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::WindowDrag,
                                                QStringLiteral("cancelSnap"));
}

void SnapHandler::handleMinimizeChanged(const QString& windowId, const QString& screenId, bool minimized)
{
    // Snap-mode-only: the autotile handler runs its own snap-state / float-state
    // machine for autotile screens.
    if (m_effect->autotileHandler()->isAutotileScreen(screenId)) {
        return;
    }

    if (minimized) {
        if (m_effect->isWindowFloating(windowId)) {
            qCDebug(lcEffect) << "Snap: minimized already-floating window, skipping float:" << windowId;
            return;
        }
        m_minimizeFloatedWindows.insert(windowId);
    } else {
        if (!m_minimizeFloatedWindows.remove(windowId)) {
            qCDebug(lcEffect) << "Snap: unminimized window was not minimize-floated, skipping unfloat:" << windowId;
            return;
        }
    }

    qCInfo(lcEffect) << "Snap: window" << (minimized ? "minimized, floating:" : "unminimized, unfloating:") << windowId
                     << "on" << screenId;

    if (m_effect->m_daemonServiceRegistered) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("setWindowFloatingForScreen"),
                                                       {windowId, screenId, minimized},
                                                       QStringLiteral("setWindowFloatingForScreen"));
    }
}

void SnapHandler::slotSnapAssistReady(const QString& windowId, const QString& releaseScreenId,
                                      const PhosphorProtocol::EmptyZoneList& emptyZones)
{
    // Discard if a new drag has already started — this signal was from a
    // prior drop. The daemon defers the compute to after endDrag returns,
    // so by the time this slot fires the user may already be dragging again.
    if (m_effect->m_dragTracker->isDragging()) {
        qCDebug(lcEffect) << "Discarding snapAssistReady: new drag in progress";
        return;
    }
    if (emptyZones.isEmpty() || releaseScreenId.isEmpty()) {
        return;
    }
    m_effect->m_snapAssistHandler->asyncShow(windowId, releaseScreenId, emptyZones);
}

void SnapHandler::slotMoveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId, int x, int y,
                                                        int width, int height)
{
    QRect geometry(x, y, width, height);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: invalid geometry" << geometry;
        return;
    }

    // Match by exact full window ID (appId|uuid) to distinguish
    // multiple windows of the same application. Fall back to appId only if
    // the exact match fails (e.g. window was recreated between candidate build
    // and selection).
    KWin::EffectWindow* targetWindow = nullptr;
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        // !isDeleted: a close-grabbed dying instance can still carry the
        // exact requested id (the recreated-window scenario the appId
        // fallback below exists for) — snapping it would track a dead id
        // with no future close event to clean it, and block the fallback
        // from finding the live sibling.
        if (w && !w->isDeleted() && m_effect->shouldHandleWindow(w) && m_effect->getWindowId(w) == windowId) {
            targetWindow = w;
            break;
        }
    }
    if (!targetWindow) {
        // appId fallback (window recreated between candidate build and
        // selection) — only when UNAMBIGUOUS: with two same-app windows,
        // taking the first stacking-order match would snap (and track) the
        // wrong sibling. Mirrors findWindowById's matchCount guard.
        const QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
        KWin::EffectWindow* appMatch = nullptr;
        int matchCount = 0;
        for (KWin::EffectWindow* w : windows) {
            if (w && !w->isDeleted() && m_effect->shouldHandleWindow(w)
                && ::PhosphorIdentity::WindowId::extractAppId(m_effect->getWindowId(w)) == appId) {
                appMatch = w;
                ++matchCount;
            }
        }
        if (matchCount == 1) {
            targetWindow = appMatch;
        }
    }

    if (!targetWindow) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: window not found" << windowId;
        m_effect->emitNavigationFeedback(false, QStringLiteral("snap_assist"), QStringLiteral("window_not_found"));
        return;
    }

    // Capture geometry BEFORE applyWindowGeometry resizes the window. The async D-Bus
    // callback in ensurePreSnapGeometryStored would read frameGeometry() after the
    // resize, corrupting the pre-tile entry with zone dimensions.
    ensurePreSnapGeometryStored(targetWindow, m_effect->getWindowId(targetWindow), targetWindow->frameGeometry());
    m_effect->applyWindowGeometry(targetWindow, geometry);

    // Derive screen from the applied geometry center. Use resolveEffectiveScreenId
    // to get the virtual screen ID (not just the physical output).
    QPoint geoCenter = geometry.center();
    const auto* output = KWin::effects->screenAt(geoCenter);
    QString screenId =
        output ? m_effect->resolveEffectiveScreenId(geoCenter, output) : m_effect->getWindowScreenId(targetWindow);

    if (m_effect->isDaemonReady("snap assist windowSnapped")) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Snap,
                                                       QStringLiteral("windowSnapped"),
                                                       {m_effect->getWindowId(targetWindow), zoneId, screenId});
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Snap,
                                                       QStringLiteral("recordSnapIntent"),
                                                       {m_effect->getWindowId(targetWindow), true});

        const bool isAutotile = m_effect->autotileHandler()->isAutotileScreen(screenId);

        // Snap-assist placed the window in a zone — record it in snapping's
        // border set, but only for a resolved snap-mode screen. An empty
        // (unresolved) or autotile-managed screen is owned by AutotileHandler,
        // so recording it here would double-track the window — same
        // discriminator as slotApplyGeometryRequested / the async snap path.
        if (!screenId.isEmpty() && !isAutotile) {
            markWindowSnapped(m_effect->getWindowId(targetWindow), screenId);
        }

        // Snap Assist continuation: only for manual-mode screens.
        // Autotile screens manage their own window placement; showing snap assist
        // after an autotile resnap is incorrect (the daemon silently ignores the
        // selection anyway via the isAutotileScreen guard in signals.cpp).
        if (!isAutotile) {
            m_effect->m_snapAssistHandler->showContinuationIfNeeded(screenId);
        }
    }
}

void SnapHandler::slotSnapAllWindowsRequested(const QString& screenId)
{
    qCInfo(lcEffect) << "Snap all windows requested for screen:" << screenId;

    if (!m_effect->isDaemonReady("snap all windows")) {
        return;
    }

    // Async fetch all snapped windows to filter already-snapped ones locally
    QDBusPendingCall snapCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);

    connect(snapWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* sw) {
        sw->deleteLater();

        QDBusPendingReply<QStringList> snapReply = *sw;
        QSet<QString> snappedFullIds;
        QSet<QString> snappedAppIds;
        if (snapReply.isValid()) {
            for (const QString& id : snapReply.value()) {
                snappedFullIds.insert(id);
                snappedAppIds.insert(::PhosphorIdentity::WindowId::extractAppId(id));
            }
        }

        // Collect unsnapped, non-floating windows on this screen in stacking order
        // (bottom-to-top) so lower windows get lower-numbered zones deterministically
        QStringList unsnappedWindowIds;
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* w : windows) {
            // !isDeleted: a close-grabbed dying window would get a zone
            // assigned under a dead id (slotWindowClosed already ran, so
            // nothing ever cleans the resulting snap record).
            if (!w || w->isDeleted() || !m_effect->shouldHandleWindow(w)) {
                continue;
            }

            QString windowId = m_effect->getWindowId(w);
            QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);

            // User-initiated snap commands override floating state.
            // windowSnapped() on the daemon clears floating inside SnapEngine::commitSnap (clearFloatingForSnap).

            // Always use EDID-based screen ID for comparison
            QString winScreen = m_effect->getWindowScreenId(w);
            if (winScreen != screenId) {
                qCDebug(lcEffect) << "snap-all: skipping window on different screen" << appId;
                continue;
            }

            if (w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
                qCDebug(lcEffect) << "snap-all: skipping minimized/other-desktop window" << appId;
                continue;
            }

            // Full ID match first (distinguishes multi-instance apps),
            // appId fallback for single-instance apps
            if (snappedFullIds.contains(windowId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window" << appId;
                continue;
            }
            if (!m_effect->hasOtherWindowOfClassWithDifferentPid(w) && snappedAppIds.contains(appId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window (appId match)" << appId;
                continue;
            }

            unsnappedWindowIds.append(windowId);
        }

        qCDebug(lcEffect) << "snap-all: found" << unsnappedWindowIds.size() << "unsnapped windows to snap";

        if (unsnappedWindowIds.isEmpty()) {
            qCDebug(lcEffect) << "No unsnapped windows to snap on screen" << screenId;
            m_effect->emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_unsnapped_windows"),
                                             QString(), QString(), screenId);
            return;
        }

        if (!m_effect->isDaemonReady("snap all windows calculation")) {
            return;
        }

        // Ask daemon to calculate zone assignments
        QDBusPendingCall calcCall = PhosphorProtocol::ClientHelpers::asyncCall(
            PhosphorProtocol::Service::Interface::Snap, QStringLiteral("calculateSnapAllWindows"),
            {QVariant::fromValue(unsnappedWindowIds), screenId});
        auto* calcWatcher = new QDBusPendingCallWatcher(calcCall, this);

        connect(calcWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* cw) {
            cw->deleteLater();

            QDBusPendingReply<PhosphorProtocol::SnapAllResultList> calcReply = *cw;
            if (calcReply.isError()) {
                qCWarning(lcEffect) << "calculateSnapAllWindows failed:" << calcReply.error().message();
                m_effect->emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("calculation_error"),
                                                 QString(), QString(), screenId);
                return;
            }

            PhosphorProtocol::SnapAllResultList snapResults = calcReply.value();

            // Build WindowGeometryList for the batch geometry path
            PhosphorProtocol::WindowGeometryList snapGeometries;
            snapGeometries.reserve(snapResults.size());
            for (const auto& r : snapResults) {
                snapGeometries.append(r.toGeometryEntry());
            }
            m_effect->slotApplyGeometriesBatch(snapGeometries, QStringLiteral("snap_all"));

            // Confirm snap assignments with daemon
            if (m_effect->isDaemonReady("snap-all confirmation")) {
                PhosphorProtocol::SnapConfirmationList confirmEntries;
                for (const auto& r : snapResults) {
                    PhosphorProtocol::SnapConfirmationEntry entry;
                    entry.windowId = r.windowId;
                    entry.zoneId = r.targetZoneId;
                    entry.screenId = screenId;
                    entry.isRestore = false;
                    confirmEntries.append(entry);
                }
                if (!confirmEntries.isEmpty()) {
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        m_effect, PhosphorProtocol::Service::Interface::Snap, QStringLiteral("windowsSnappedBatch"),
                        {QVariant::fromValue(confirmEntries)}, QStringLiteral("windowsSnappedBatch"));
                }
            }
        });
    });
}

void SnapHandler::slotPendingRestoresAvailable()
{
    // If slotDaemonReady already dispatched snap restores for this daemon
    // session, skip — both signals fire during restart, and the second round
    // of moveResize() calls would disrupt the stacking order that the first
    // round carefully preserves via activateWindow(previouslyActive).
    if (m_effect->m_daemonReadyRestoresDone) {
        qCInfo(lcEffect) << "Pending restores: already handled by slotDaemonReady, skipping";
        return;
    }

    qCInfo(lcEffect) << "Pending restores: retrying restoration for all visible windows";

    if (!m_effect->isDaemonReady("pending restores")) {
        return;
    }

    // Use ASYNC batch call to get all tracked windows at once
    QDBusPendingCall pendingCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QStringList> reply = *w;
        QSet<QString> trackedWindowIds;

        if (reply.isValid()) {
            // Track by FULL windowId (appId|uuid), NOT appId. A multi-window app
            // (e.g. several ghostty terminals, each snapped to its own zone) has one
            // tracked entry PER window; deduping by appId would treat the whole app
            // as "handled" the moment ONE of its windows restored, and skip every
            // sibling below — including a window that individually failed its early
            // restore (it raced startup and got a not-ready no-snap) and is the exact
            // window this retry net exists to recover. The daemon tracks restored
            // windows by their live id, which matches getWindowId() here.
            const QStringList trackedWindows = reply.value();
            for (const QString& windowId : trackedWindows) {
                if (!windowId.isEmpty()) {
                    trackedWindowIds.insert(windowId);
                }
            }
            qCDebug(lcEffect) << "Got" << trackedWindowIds.size() << "tracked windows from daemon";
        } else {
            qCWarning(lcEffect) << "Failed to get tracked windows:" << reply.error().message();
            // Continue anyway - will try to restore all windows (daemon will handle duplicates)
        }

        // Now iterate through all visible windows and restore untracked ones
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* window : windows) {
            // !isDeleted: a close-grabbed dying window would consume the
            // single-shot FIFO pending-restore entry for its appId, robbing
            // the app's next REAL window of its restore.
            if (!window || window->isDeleted() || !m_effect->shouldHandleWindow(window)) {
                continue;
            }

            // Skip minimized or invisible windows
            if (window->isMinimized() || !window->isOnCurrentDesktop() || !window->isOnCurrentActivity()) {
                continue;
            }

            // Check if THIS window is already tracked (exact id, O(1)). A snapped
            // sibling of the same app no longer masks an untracked window here.
            QString windowId = m_effect->getWindowId(window);
            if (trackedWindowIds.contains(windowId)) {
                continue; // Already tracked
            }

            // Window is not tracked - try to restore it
            qCDebug(lcEffect) << "Retrying restoration for untracked window:" << windowId;
            callResolveWindowRestore(window);
        }
    });
}

} // namespace PlasmaZones
