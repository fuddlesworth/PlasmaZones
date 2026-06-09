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

namespace {
// Drop the server-side decoration while keeping the window filling its zone.
// KWin holds the CLIENT size constant across a decoration change, so calling
// setNoBorder(true) AFTER the zone geometry was applied shrinks the frame by the
// title-bar height and leaves a gap at the bottom of the zone. Re-assert the
// zone rect after dropping the decoration so the content grows to fill it.
//
// CRITICAL: use moveResizeGeometry(), NOT frameGeometry(). On Wayland the latter
// lags behind moveResize() until the client acks the configure, so right after
// applySnapGeometry's moveResize it still reports the window's PRE-snap frame —
// capturing and re-applying that would clobber the snap (the window reverts to
// its floating size/position; this broke snap restore entirely). moveResizeGeometry()
// is the zone rect KWin is already moving toward, set synchronously by moveResize.
// The setNoBorder→moveResize ordering mirrors autotile hiding the title bar
// BEFORE its moveResize (autotilehandler/state.cpp::setWindowBorderless).
void hideTitleBarFillingZone(KWin::Window* kw)
{
    const KWin::RectF zoneTarget = kw->moveResizeGeometry();
    kw->setNoBorder(true);
    // Only re-assert a real target. A degenerate move-resize geometry would
    // otherwise resize the window to nothing; in that case leave the decoration
    // change to settle on its own.
    if (zoneTarget.isValid() && !zoneTarget.isEmpty()) {
        kw->moveResize(zoneTarget);
    }
}
} // namespace

SnapHandler::SnapHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void SnapHandler::markWindowSnapped(const QString& windowId, const QString& screenId)
{
    // An empty screenId is never a valid snap owner: the per-screen buckets are
    // keyed by screenId, so recording under "" would pollute the set with an
    // entry that the per-screen stripOtherScreens cleanup can never reclaim.
    // Callers route unresolved/float windows through clearWindowSnapped instead;
    // this guard is defensive depth for any path that slips an empty screen in.
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    // A window can only be snap-managed by one screen at a time. Strip stale
    // tracking from any OTHER screen — both the tiled and borderless buckets —
    // before recording the new owner (mirrors the autotile cross-screen-transfer
    // cleanup in tiling.cpp).
    const auto stripOtherScreens = [&](QHash<QString, QSet<QString>>& byScreen) {
        for (auto it = byScreen.begin(); it != byScreen.end();) {
            if (it.key() != screenId) {
                it.value().remove(windowId);
            }
            if (it.value().isEmpty() && it.key() != screenId) {
                it = byScreen.erase(it);
            } else {
                ++it;
            }
        }
    };
    stripOtherScreens(m_border.tiledWindowsByScreen);
    stripOtherScreens(m_border.borderlessWindowsByScreen);
    AutotileStateHelpers::addTiledOnScreen(m_border, screenId, windowId);

    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
    KWin::Window* kw = w ? w->window() : nullptr;
    // userCanSetNoBorder() — not hasDecoration() — is the correct test for "may
    // this window's server-side title bar be toggled off." It reports whether KWin
    // ALLOWS the no-border toggle, so it stays true for a normal SSD window even
    // while that window is CURRENTLY borderless — exactly the autotile→snap handoff
    // case, where the window arrives already borderless (autotile stripped its
    // decoration) and hasDecoration() therefore reads false. It is false for windows
    // that have no server-side title bar to hide in the first place — client-side-
    // decorated apps (GTK/Electron) and other non-toggleable windows (override-
    // redirect, or decoration forced by a window rule). Using hasDecoration() here
    // skipped the handoff, so snap never recorded the borderless ownership and the
    // deferred restoreWindowBorders un-hid the title bar autotile had hidden.
    // (AutotileHandler::setWindowBorderless still gates on hasDecoration(); the snap
    // side intentionally diverges because only it must survive the already-borderless
    // handoff.)
    if (m_border.hideTitleBars && kw && kw->userCanSetNoBorder()) {
        const bool wasBorderless = AutotileStateHelpers::isBorderlessWindow(m_border, windowId);
        AutotileStateHelpers::addBorderlessOnScreen(m_border, screenId, windowId);
        if (!wasBorderless) {
            hideTitleBarFillingZone(kw);
        }
    }
    // A null w means the window is gone (closed mid-snap); the bucket entry
    // recorded above is then harmless — if the window later closes, slotWindowClosed
    // clears the snap border for it. No border is drawn (nothing to draw on) and none
    // is needed; updateAllBorders() iterates only live windows so it simply skips it.
    //
    // Border overlays are visual-only, so skip the off-desktop case (consistent
    // with updateAllBorders): an OutlinedBorderItem for an invisible window is
    // wasted work. When the user switches to that window's desktop, the
    // desktopChanged → updateAllBorders connection rebuilds its border.
    if (w && w->isOnCurrentDesktop()) {
        m_effect->updateWindowBorder(windowId, w);
    }
}

void SnapHandler::clearWindowSnapped(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    const bool wasBorderless = AutotileStateHelpers::isBorderlessWindow(m_border, windowId);
    AutotileStateHelpers::removeFromAllScreens(m_border, windowId);
    // Restore the title bar only if snap had hidden it AND autotile doesn't
    // still want it borderless. A window mid-transition between modes can be in
    // both sets briefly; un-hiding here would fight autotile's authoritative
    // borderless management and flash the title bar.
    if (wasBorderless && !m_effect->autotileHandler()->isBorderlessWindow(windowId)) {
        if (KWin::EffectWindow* w = m_effect->findWindowById(windowId)) {
            if (KWin::Window* kw = w->window()) {
                kw->setNoBorder(false);
            }
        }
    }
    m_effect->removeWindowBorder(windowId);
}

void SnapHandler::updateSnapHideTitleBars(bool hide)
{
    m_border.hideTitleBars = hide;
    if (hide) {
        // Hide on every currently snap-committed window.
        const auto pairs = AutotileStateHelpers::allTiledPairs(m_border);
        for (const auto& p : pairs) {
            KWin::EffectWindow* w = m_effect->findWindowById(p.first);
            if (!w || !w->hasDecoration()) {
                continue;
            }
            if (!AutotileStateHelpers::isBorderlessWindow(m_border, p.first)) {
                AutotileStateHelpers::addBorderlessOnScreen(m_border, p.second, p.first);
                if (KWin::Window* kw = w->window()) {
                    hideTitleBarFillingZone(kw);
                }
            }
        }
    } else {
        // Restore every window snap had made borderless, except one autotile
        // still wants borderless. A window mid-transition between modes can be in
        // both sets briefly; un-hiding here would fight autotile's authoritative
        // borderless management and flash the title bar (mirrors the guard in
        // clearWindowSnapped / restoreAllSnapBorderless).
        const auto pairs = AutotileStateHelpers::allBorderlessPairs(m_border);
        for (const auto& p : pairs) {
            AutotileStateHelpers::removeBorderlessOnScreen(m_border, p.second, p.first);
            if (m_effect->autotileHandler()->isBorderlessWindow(p.first)) {
                continue;
            }
            if (KWin::EffectWindow* w = m_effect->findWindowById(p.first)) {
                if (KWin::Window* kw = w->window()) {
                    kw->setNoBorder(false);
                }
            }
        }
    }
    m_effect->updateAllBorders();
}

void SnapHandler::restoreAllSnapBorderless()
{
    // Symmetric with AutotileHandler::restoreAllBorderless: on daemon loss or
    // effect teardown the authoritative snap state is gone, so restore every
    // title bar snapping hid and drop the whole snap border set. Without this,
    // snap-hidden windows would keep their title bars hidden until a new snap
    // event or app restart. The isBorderlessWindow guard below is the live
    // protection in the per-window path (clearWindowSnapped); at the teardown
    // call sites AutotileHandler::restoreAllBorderless() runs first and clears
    // its set, so the guard is a belt-and-braces no-op here (a window autotile
    // shares is already un-hidden by then, and a second setNoBorder(false) is
    // harmless/idempotent).
    // This drops the snap border STATE only; callers pair it with
    // clearAllBorders() to tear down the OutlinedBorderItem scene items.
    const auto pairs = AutotileStateHelpers::allBorderlessPairs(m_border);
    for (const auto& p : pairs) {
        if (m_effect->autotileHandler()->isBorderlessWindow(p.first)) {
            continue;
        }
        if (KWin::EffectWindow* w = m_effect->findWindowById(p.first)) {
            if (KWin::Window* kw = w->window()) {
                kw->setNoBorder(false);
            }
        }
    }
    m_border.borderlessWindowsByScreen.clear();
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
    QRectF geom = preCapturedGeometry.isValid() ? preCapturedGeometry : w->frameGeometry();
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
    PhosphorProtocol::ClientHelpers::fireAndForget(
        m_effect, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
        {windowId, static_cast<int>(geom.x()), static_cast<int>(geom.y()), static_cast<int>(geom.width()),
         static_cast<int>(geom.height()), screenId, false},
        QStringLiteral("storePreTileGeometry"));
    qCInfo(lcEffect) << "Stored pre-tile geometry for window" << windowId << "geom=" << geom;
}

void SnapHandler::handleCursorMoved(const QPointF& pos, const QString& screenId)
{
    if (!m_focusFollowsMouse) {
        return;
    }

    // Pause FFM while a transient/popup/special window is active so hovering a
    // snapped window beneath it does not dismiss it — e.g. an emoji picker or
    // notification opened over a snapped window, where moving the cursor across
    // the underlying window's exposed area would otherwise activate it and send
    // the popup to the background. A snapped or normal tileable active window
    // does not pause FFM. Scoped to the cursor's screen (mirrors
    // AutotileHandler::handleCursorMoved, discussion #461): a transient window
    // active on another monitor must not freeze FFM on the monitor the cursor is
    // on. Our own full-screen overlays never count as the kind of active window
    // worth protecting.
    if (KWin::EffectWindow* active = KWin::effects->activeWindow()) {
        // Cheap overlay-class check first, then the heavier screen resolution
        // (mirrors the autotile guard's predicate ordering). The predicate is
        // deliberately wider than the under-cursor occlusion guard below: there
        // we only look *through* a snapped window, but here a normal tileable
        // active window (a regular app the user is working in, not a popup) must
        // not pause FFM either. Only a non-snapped, non-tileable active window
        // (dialog/popup/excluded app) is worth protecting. Both of autotile's
        // guards key off the same tileable/shouldHandle membership; snap's managed
        // set (snapped) is narrower than tileable, so here the pause guard accepts
        // the extra isTileableWindow case the occlusion guard below does not.
        if (!PlasmaZonesEffect::isOwnOverlayClass(active->windowClass())
            && m_effect->getWindowScreenId(active) == screenId && !isTiledWindow(m_effect->getWindowId(active))
            && !m_effect->isTileableWindow(active)) {
            return;
        }
    }

    // Find the topmost snapped window under the cursor (stacking order top → bottom).
    const auto windows = KWin::effects->stackingOrder();
    for (int i = windows.size() - 1; i >= 0; --i) {
        KWin::EffectWindow* w = windows[i];
        if (!w || w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
            continue;
        }
        // Cheap geometry test before the windowClass()/windowId allocations below.
        if (!w->frameGeometry().contains(pos)) {
            continue;
        }
        // Look through our own overlay/editor layer-shell surfaces — they are full-screen
        // and always topmost, so a bail here would kill FFM whenever an overlay is up
        // (mirrors the autotile FFM guard).
        if (PlasmaZonesEffect::isOwnOverlayClass(w->windowClass())) {
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
        if (w && m_effect->shouldHandleWindow(w) && m_effect->getWindowId(w) == windowId) {
            targetWindow = w;
            break;
        }
    }
    if (!targetWindow) {
        QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
        for (KWin::EffectWindow* w : windows) {
            if (w && m_effect->shouldHandleWindow(w)
                && ::PhosphorIdentity::WindowId::extractAppId(m_effect->getWindowId(w)) == appId) {
                targetWindow = w;
                break;
            }
        }
    }

    if (!targetWindow) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: window not found" << windowId;
        m_effect->emitNavigationFeedback(false, QStringLiteral("snap_assist"), QStringLiteral("window_not_found"));
        return;
    }

    // Capture geometry BEFORE applySnapGeometry resizes the window. The async D-Bus
    // callback in ensurePreSnapGeometryStored would read frameGeometry() after the
    // resize, corrupting the pre-tile entry with zone dimensions.
    ensurePreSnapGeometryStored(targetWindow, m_effect->getWindowId(targetWindow), targetWindow->frameGeometry());
    m_effect->applySnapGeometry(targetWindow, geometry);

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

        // Snap-assist placed the window in a zone — record it in snapping's
        // border set, but only for a resolved snap-mode screen. An empty
        // (unresolved) or autotile-managed screen is owned by AutotileHandler,
        // so recording it here would double-track the window — same
        // discriminator as slotApplyGeometryRequested / the async snap path.
        if (!screenId.isEmpty() && !m_effect->autotileHandler()->isAutotileScreen(screenId)) {
            markWindowSnapped(m_effect->getWindowId(targetWindow), screenId);
        }

        // Snap Assist continuation: only for manual-mode screens.
        // Autotile screens manage their own window placement; showing snap assist
        // after an autotile resnap is incorrect (the daemon silently ignores the
        // selection anyway via the isAutotileScreen guard in signals.cpp).
        if (!m_effect->autotileHandler()->isAutotileScreen(screenId)) {
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
            if (!w || !m_effect->shouldHandleWindow(w)) {
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
        QSet<QString> trackedAppIds;

        if (reply.isValid()) {
            // Extract app IDs from tracked windows for comparison
            const QStringList trackedWindows = reply.value();
            for (const QString& windowId : trackedWindows) {
                QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
                if (!appId.isEmpty()) {
                    trackedAppIds.insert(appId);
                }
            }
            qCDebug(lcEffect) << "Got" << trackedAppIds.size() << "tracked windows from daemon";
        } else {
            qCWarning(lcEffect) << "Failed to get tracked windows:" << reply.error().message();
            // Continue anyway - will try to restore all windows (daemon will handle duplicates)
        }

        // Now iterate through all visible windows and restore untracked ones
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* window : windows) {
            if (!window || !m_effect->shouldHandleWindow(window)) {
                continue;
            }

            // Skip minimized or invisible windows
            if (window->isMinimized() || !window->isOnCurrentDesktop() || !window->isOnCurrentActivity()) {
                continue;
            }

            // Check if this window is already tracked using local set lookup (O(1))
            QString windowId = m_effect->getWindowId(window);
            QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
            if (trackedAppIds.contains(appId)) {
                continue; // Already tracked
            }

            // Window is not tracked - try to restore it
            qCDebug(lcEffect) << "Retrying restoration for untracked window:" << windowId;
            callResolveWindowRestore(window);
        }
    });
}

} // namespace PlasmaZones
