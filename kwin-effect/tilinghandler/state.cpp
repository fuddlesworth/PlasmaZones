// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilinghandler.h"
#include "handlers/navigationhandler.h"
#include "handlers/snaphandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effectwindow.h>
#include <window.h>

#include <QAction>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// ═══════════════════════════════════════════════════════════════════════════════
// Monocle helpers
// ═══════════════════════════════════════════════════════════════════════════════

void TilingHandler::unmaximizeMonocleWindow(const QString& windowId)
{
    if (!m_monocleMaximizedWindows.remove(windowId)) {
        return;
    }
    // EXACT resolve: a stale monocle entry whose window is gone must restore
    // nothing — the fuzzy appId fallback would un-maximize an unrelated
    // same-app sibling under suppression, invisibly.
    KWin::EffectWindow* w = m_effect->findWindowByIdExact(windowId);
    if (!w) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    // maximize() emits windowFrameGeometryChanged SYNCHRONOUSLY, and the
    // restore rect can sit in a different virtual-screen region of the same
    // monitor. Without the geometry-apply gate that edge takes the
    // VS-crossing path (handleWindowOutputChanged -> windowClosed +
    // notifyWindowAdded) and tears down whatever float/tile transition the
    // caller is mid-way through. Save/restore rather than set/clear so the
    // guard nests inside already-guarded callers.
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    ++m_suppressMaximizeChanged;
    kw->maximize(KWin::MaximizeRestore);
    --m_suppressMaximizeChanged;
    m_effect->m_daemonGate.inGeometryApply = prevInApply;
}

void TilingHandler::restoreAllMonocleMaximized()
{
    if (m_monocleMaximizedWindows.isEmpty()) {
        return;
    }
    // Snapshot and clear FIRST: maximize() can synchronously re-enter
    // cleanupClosedWindowState (via the VS-crossing / output-changed path),
    // which mutates m_monocleMaximizedWindows — iterating the live set here
    // is iterator invalidation in a compositor loop.
    const QStringList ids = m_monocleMaximizedWindows.values();
    m_monocleMaximizedWindows.clear();
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    ++m_suppressMaximizeChanged;
    for (const QString& wid : ids) {
        // EXACT resolve — same sibling hazard as unmaximizeMonocleWindow.
        KWin::EffectWindow* w = m_effect->findWindowByIdExact(wid);
        if (w) {
            KWin::Window* kw = w->window();
            if (kw) {
                kw->maximize(KWin::MaximizeRestore);
            }
        }
    }
    --m_suppressMaximizeChanged;
    m_effect->m_daemonGate.inGeometryApply = prevInApply;
}

void TilingHandler::clearTiledTracking()
{
    // Bookkeeping only. Physical title-bar restores are the
    // DecorationManager's job — teardown callers pair this with
    // DecorationManager::restoreAll().
    m_border.tiledWindowsByScreen.clear();
    // The screen set belongs to the daemon session that published it. Both
    // callers (daemon loss, effect teardown) mean that session is gone —
    // keeping the set let stale membership answer isAutotileScreen until the
    // next bringup reply, and left the bringup's fresh-set replacement with
    // no removed-screen delta to act on.
    m_managedScreens.clear();
}

void TilingHandler::setFocusFollowsMouse(bool enabled)
{
    m_focusFollowsMouse = enabled;
}

void TilingHandler::saveAndRecordPreTileGeometry(const QString& windowId, const QString& screenId,
                                                 KWin::EffectWindow* w, const QRectF& frameIn, bool knownFreeFloating)
{
    if (windowId.isEmpty() || screenId.isEmpty()) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry save: empty id" << windowId << screenId;
        return;
    }
    // Correct for maximize/fullscreen (shared with SnapHandler's capture): a maximized
    // window's frameGeometry() is the full monitor, and storing that as the float-back
    // size floats the window back maximized. This store is the SAME daemon free-geometry
    // record snap reads, so an unguarded capture here would poison snap's restore too.
    const QRectF frame = PlasmaZonesEffect::freeGeometryForCapture(w, frameIn);
    if (!frame.isValid() || frame.width() <= 0 || frame.height() <= 0) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry save: invalid frame" << frame << "for" << windowId;
        return;
    }
    // Use EXACT windowId match only — NOT an appId/stableId fallback.
    // Multiple instances of the same app (e.g., 3 Dolphin windows) share an
    // appId; a fuzzy contains-check would return true after the first
    // instance is saved, preventing all other instances from saving their own
    // geometry. On restore, all instances would get the first instance's
    // geometry — scrambling window positions on every autotile ↔ snapping toggle.
    //
    // Use a CONST lookup for the contains-check so a guard-bail below never inserts an
    // empty per-screen bucket (operator[] would); the bucket is created only at the
    // genuine insertion point (below).
    {
        const auto screenIt = m_preTileGeometries.constFind(screenId);
        if (screenIt != m_preTileGeometries.constEnd() && screenIt->contains(windowId)) {
            return;
        }
    }
    // Only save geometry for floating windows — snapped/tiled windows have zone
    // dimensions in frameGeometry(), not the original free-floating size. Storing
    // zone geometry here would cause handleDragToFloat to restore to zone size.
    //
    // EXCEPTION: freshly-opened windows are not tracked in the FloatingCache yet,
    // so isWindowFloating() returns false even though their frame IS the authoritative
    // free-floating spawn geometry. Callers that know they are processing a fresh
    // window pass knownFreeFloating=true to bypass the guard. Without that bypass,
    // the save is silently dropped and every later float-restore for this window
    // falls through to stale cross-session data (or, with exact-only lookups, nothing).
    // A snap-managed window's frame IS its zone rect, never a free-floating
    // position — this holds EVEN on the knownFreeFloating fast path, which fires
    // when a window is re-added to autotile on a snap→autotile toggle. Storing the
    // zone rect as the pre-autotile float-back is the per-mode leak: a later
    // float-in-autotile then teleports the window to the snap zone instead of its
    // genuine pre-snap free position. isWindowFloating() below misses this because
    // knownFreeFloating bypasses it, so check the snap-managed state explicitly and
    // unconditionally.
    const SnapHandler* snap = m_effect->snapHandler();
    if (m_effect->isWindowMarkedSnapped(windowId) || (snap && snap->isMinimizeFloated(windowId))) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry for snap-owned window (frame is zone rect)" << windowId
                          << "on" << screenId;
        return;
    }
    // Own-side twin of the guard above: a window THIS handler holds as a
    // minimize-float was tiled when it minimized (the daemon-restart re-claim
    // path re-adds such windows with knownFreeFloating routing), so its frame
    // is the TILE rect. The UNTILED subset is carved out — those windows'
    // rects belong to the PRIOR mode, and the snap-owned guard above already
    // rejects zone rects, so a surviving untiled rect is a genuine free
    // position worth capturing. isMinimizeFloated (not the raw marker set):
    // a window mid-unfloat sits in m_unfloatInFlight instead, and its frame
    // is still the tile rect until the restore lands — capturing during that
    // interval is the same poison.
    if (isMinimizeFloated(windowId) && !m_untiledMinimizeFloats.contains(windowId)) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry for own minimize-float (frame is tile rect)" << windowId
                          << "on" << screenId;
        return;
    }
    if (!knownFreeFloating && !m_effect->isWindowFloating(windowId)) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry for snapped window" << windowId << "on" << screenId;
        return;
    }
    m_preTileGeometries[screenId][windowId] = frame;
    qCDebug(lcEffect) << "Saved pre-autotile geometry for" << windowId << "on" << screenId << ":" << frame;
    if (m_effect->m_daemonGate.serviceRegistered) {
        // overwrite=knownFreeFloating: only the window-opened spawn paths
        // (the sole callers passing true) may clobber a persisted daemon
        // entry — the spawn frame IS the authoritative free-floating
        // geometry, and a stale appId-keyed entry from a prior session
        // would otherwise block the fresh capture and leave float-restore
        // teleporting the window to ancient coordinates.
        // Every other caller (autotile toggle, unminimize-unfloat,
        // cross-screen transfer) pushes non-destructively: an
        // overflow-floated window can pass the isWindowFloating() guard
        // while its frame still sits at the TILED position, and an
        // overwrite there would destroy the daemon's correct free-position
        // entry — exactly what the toggle path's explicit overwrite=false
        // back-fill exists to preserve.
        // qRound, not truncation: fractional-scale sub-pixel residue (see the
        // toRect() geometry-capture convention in window_lifecycle.cpp).
        PhosphorProtocol::ClientHelpers::fireAndForget(
            m_effect, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
            {windowId, qRound(frame.x()), qRound(frame.y()), qRound(frame.width()), qRound(frame.height()), screenId,
             knownFreeFloating},
            QStringLiteral("storePreTileGeometry"));
    }
}

void TilingHandler::requestDaemonPreTileRestore(KWin::EffectWindow* w, const QString& windowId)
{
    QPointer<KWin::EffectWindow> safeW = w;
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("getValidatedPreTileGeometry"), {windowId}),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, safeW, windowId](QDBusPendingCallWatcher* pw) {
        pw->deleteLater();
        QDBusPendingReply<bool, int, int, int, int> reply = *pw;
        if (!reply.isValid() || reply.count() < 5 || !reply.argumentAt<0>()) {
            return;
        }
        const int rw = reply.argumentAt<3>();
        const int rh = reply.argumentAt<4>();
        if (rw <= 0 || rh <= 0 || !safeW || safeW->isDeleted()) {
            return;
        }
        // Anything that took (back) ownership of the window during the
        // round-trip supersedes this orphan restore: another desktop
        // switch, a re-tile (re-notified), the screen re-entering
        // autotile, a snap commit, a float toggle, or the user actively
        // moving/resizing it.
        if (!safeW->isOnCurrentDesktop() || !safeW->isOnCurrentActivity() || m_notifiedWindows.contains(windowId)
            || m_managedScreens.contains(m_effect->getWindowScreenId(safeW))
            || m_effect->isWindowMarkedSnapped(windowId) || m_effect->isWindowFloating(windowId) || safeW->isUserMove()
            || safeW->isUserResize()) {
            return;
        }
        // Suppress the VS-crossing detectors across the synchronous
        // frameGeometryChanged this apply emits — same rationale as the
        // local-bucket restore path in slotScreensChanged.
        // Save/restore, not set/clear: a clearing guard nested inside an outer
        // apply would hand the outer scope back an un-flagged window.
        const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
        m_effect->m_daemonGate.inGeometryApply = true;
        const auto geomGuard = qScopeGuard([this, prevInApply] {
            m_effect->m_daemonGate.inGeometryApply = prevInApply;
        });
        // Clear any lingering KWin maximize flag first or KWin re-asserts
        // the maximize-area rect and defeats the restore (discussion #461).
        if (KWin::Window* kw = safeW->window(); kw && kw->maximizeMode() != KWin::MaximizeRestore) {
            ++m_suppressMaximizeChanged;
            kw->maximize(KWin::MaximizeRestore);
            --m_suppressMaximizeChanged;
        }
        // Snap-out: leaving zone-managed sizing.
        m_effect->applyWindowGeometry(safeW, QRect(reply.argumentAt<1>(), reply.argumentAt<2>(), rw, rh),
                                      /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                      PhosphorAnimation::ProfilePaths::WindowSnapOut);
        qCInfo(lcEffect) << "Desktop switch: restored pre-snap geometry from daemon for orphaned window" << windowId;
    });
}

QRectF TilingHandler::findPreTileGeometry(const QString& windowId, QString* bucketScreenId) const
{
    for (auto sgIt = m_preTileGeometries.constBegin(); sgIt != m_preTileGeometries.constEnd(); ++sgIt) {
        const QRectF rect = sgIt->value(windowId);
        if (rect.isValid()) {
            if (bucketScreenId) {
                *bucketScreenId = sgIt.key();
            }
            return rect;
        }
        // Found-but-invalid entry: keep scanning. A valid rect may still be
        // stored under another screen's bucket from a mid-session
        // autotile-screen transfer.
    }
    return QRectF();
}

bool TilingHandler::isManagedScreen(const QString& screenId) const
{
    return m_managedScreens.contains(screenId);
}

void TilingHandler::slotScrollingScreensChanged(const QStringList& screenIds)
{
    // Mode discriminator — no per-screen LIFECYCLE transitions here (the
    // union set arriving via slotScreensChanged owns those). But the set IS
    // an input to ruleQuery's Mode stamp, and rule verdicts are memoised per
    // window: on an autotile↔scrolling flip the union does not move, so
    // slotScreensChanged never invalidates anything and a `Mode Equals
    // "scrolling"` border/opacity/decoration rule would keep its stale
    // verdict indefinitely. Invalidate + sweep on a GENUINE change only
    // (identical-set desktop-switch re-emits stay free).
    setScrollingScreens(QSet<QString>(screenIds.cbegin(), screenIds.cend()));
}

void TilingHandler::setScrollingScreens(const QSet<QString>& newSet)
{
    // Any authoritative write voids in-flight property replies, identical
    // set or not — the writer is always newer than a reply dispatched
    // earlier (see the m_scrollingScreensGeneration doc).
    ++m_scrollingScreensGeneration;
    if (newSet == m_scrollingScreens) {
        return;
    }
    const QSet<QString> oldSet = m_scrollingScreens;
    m_scrollingScreens = newSet;
    m_effect->invalidateAllRuleCaches();
    m_effect->scheduleBorderSweep();

    // Engine-flip re-announce. A screen that changes tiling ENGINE while
    // staying in the union (autotile↔scrolling) never transits
    // managedScreensChanged — the union is emit-on-change and does not move —
    // so slotScreensChanged cannot demote and re-announce its windows. The
    // daemon side has already torn the old engine's state down and the new
    // engine claims an EMPTY screen: windows keep their old rects and every
    // verb on the new engine refuses. Re-announce the flipped screens'
    // windows here; the daemon routes windowOpened by the screen's current
    // mode, so the receiving engine adopts them (order-seeded from the
    // capture the daemon took during the flip). Cross-union transitions
    // (snapping↔scrolling) still announce exactly once regardless of which
    // signal lands first: whichever handler sees the screen inside
    // m_managedScreens does the work, the other filters it out
    // (notifyWindowsAddedBatch drops screens outside the union, and
    // slotScreensChanged only processes union membership changes).
    QSet<QString> flipped = (newSet - oldSet) + (oldSet - newSet);
    flipped &= m_managedScreens;
    if (!flipped.isEmpty()) {
        qCInfo(lcEffect) << "Scrolling flip within managed union — re-announcing windows on" << flipped;
        notifyWindowsAddedBatch(KWin::effects->stackingOrder(), flipped, /*resetNotified=*/true);
    }
    updateScrollWheelShortcuts();
}

void TilingHandler::updateScrollWheelShortcuts()
{
    const bool want = !m_scrollingScreens.isEmpty();
    if (want == !m_scrollWheelActions.isEmpty()) {
        return;
    }
    if (!want) {
        // Destroying the QAction unregisters the axis shortcut (KWin's
        // shortcut manager tracks action lifetime), releasing Meta+wheel
        // back to other consumers (e.g. the zoom effect).
        qDeleteAll(m_scrollWheelActions);
        m_scrollWheelActions.clear();
        qCInfo(lcEffect) << "Scroll wheel shortcuts unregistered (no scrolling screens)";
        return;
    }
    // niri's default Mod+wheel bindings: wheel down / right focuses the
    // next column to the right, wheel up / left the previous one. The
    // horizontal pair covers tilted wheels and two-finger horizontal
    // touchpad scrolls.
    const auto add = [this](KWin::PointerAxisDirection axis, int delta, const char* name) {
        auto* action = new QAction(this);
        action->setObjectName(QLatin1String(name));
        connect(action, &QAction::triggered, this, [this, delta]() {
            wheelFocusColumn(delta);
        });
        KWin::effects->registerAxisShortcut(Qt::MetaModifier, axis, action);
        m_scrollWheelActions.append(action);
    };
    add(KWin::PointerAxisDown, 1, "plasmazones-scroll-focus-column-right");
    add(KWin::PointerAxisUp, -1, "plasmazones-scroll-focus-column-left");
    add(KWin::PointerAxisRight, 1, "plasmazones-scroll-focus-column-right-h");
    add(KWin::PointerAxisLeft, -1, "plasmazones-scroll-focus-column-left-h");
    qCInfo(lcEffect) << "Scroll wheel shortcuts registered (Meta+wheel focuses columns)";
}

void TilingHandler::wheelFocusColumn(int delta)
{
    if (!m_effect->m_daemonGate.serviceRegistered) {
        return;
    }
    // The strip that moves is the one under the CURSOR (Meta+wheel is a
    // pointer gesture, not a focus verb): resolve the cursor's effective
    // screen — virtual subdivisions included — and only forward when it
    // actually runs the scrolling engine. On any other screen the chord is
    // consumed but inert; registration is per-session, not per-screen.
    const QPointF pos = KWin::effects->cursorPos();
    const QPoint rounded(qRound(pos.x()), qRound(pos.y()));
    const auto* output = KWin::effects->screenAt(rounded);
    if (!output) {
        return;
    }
    const QString screenId = m_effect->resolveEffectiveScreenId(rounded, output);
    if (!m_scrollingScreens.contains(screenId)) {
        return;
    }
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Scrolling,
                                                   QStringLiteral("focusColumn"), {screenId, delta});
}

void TilingHandler::savePreTileForDesktopMove(const QString& windowId)
{
    // Preserve the window's pre-autotile geometry before onWindowClosed clears it.
    // When the window is re-added on the target desktop, this geometry is restored
    // so that float-restore returns to the original position, not the tiled frame.
    //
    // Stamped with the BUCKET's screen (not the caller's) so the restore
    // path can detect a cross-screen desktop move and decline a saved rect
    // from a different monitor's coordinate space.
    QString bucketScreenId;
    const QRectF rect = findPreTileGeometry(windowId, &bucketScreenId);
    if (rect.isValid()) {
        m_savedPreTileForDesktopMove[windowId] = {bucketScreenId, rect};
        qCDebug(lcEffect) << "Preserved pre-autotile geometry for desktop move:" << windowId << "bucket"
                          << bucketScreenId << "rect=" << rect;
    }
}

bool TilingHandler::isEligibleForTilingNotify(KWin::EffectWindow* w, bool* rejectedOnlyBecauseMinimized) const
{
    if (rejectedOnlyBecauseMinimized) {
        *rejectedOnlyBecauseMinimized = false;
    }
    // Close-grabbed dying windows survive in the stacking order for the
    // close-animation duration; announcing one as opened would insert an
    // orphan into the tiling tree (shrinking live tiles) until a later
    // retile cleans it up.
    if (w && w->isDeleted()) {
        return false;
    }
    // Early-out: KWin internal surfaces (overlay QQuickViews, zone overlays, etc.)
    // are never eligible for autotile notification. KWin's InternalWindow::minSize()
    // segfaults when the backing QWindow is null. See discussion #511.
    if (w && w->window() && w->window()->isInternal()) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (internal window)" << m_effect->getWindowId(w);
        return false;
    }
    if (!w || !m_effect->shouldHandleWindow(w)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (not handleable)"
                          << (w ? m_effect->getWindowId(w) : QStringLiteral("null"));
        return false;
    }
    if (!m_effect->isTileableWindow(w)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (not tileable)" << m_effect->getWindowId(w);
        return false;
    }
    // A window that is fullscreen at first contact (opened fullscreen, or
    // present-fullscreen when autotile is enabled / the daemon restarts):
    // KWin owns its geometry and re-asserts the fullscreen frame, so
    // announcing it would (a) push the fullscreen frame as free geometry
    // with overwrite=true and (b) make the daemon try to tile a window KWin
    // won't let move. The exit-fullscreen slot re-announces it via
    // notifyWindowAdded once it returns to a normal frame.
    if (w->isFullScreen()) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (fullscreen)" << m_effect->getWindowId(w);
        return false;
    }
    if (!w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (wrong desktop/activity)" << m_effect->getWindowId(w);
        return false;
    }
    // Reject windows smaller than the user-configured minimum size.
    // Prevents small utility windows (emoji picker, color picker, etc.)
    // from entering the tiling tree and disrupting the layout.
    const QRectF frame = w->frameGeometry();
    if ((m_effect->m_cachedMinWindowWidth > 0 && frame.width() < m_effect->m_cachedMinWindowWidth)
        || (m_effect->m_cachedMinWindowHeight > 0 && frame.height() < m_effect->m_cachedMinWindowHeight)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (too small)" << m_effect->getWindowId(w)
                          << "size=" << frame.size() << "threshold=" << m_effect->m_cachedMinWindowWidth << "x"
                          << m_effect->m_cachedMinWindowHeight;
        return false;
    }
    // Checked LAST so the out-flag means "every other gate passed": the batch
    // announce claims such a window as minimize-floated (minimizedChanged
    // never fires for a window that was already minimized when its screen
    // entered autotile, so the runtime minimize→float path cannot cover it).
    // frameGeometry stays at the pre-minimize frame while minimized, so the
    // min-size gate above evaluates real dimensions for this ordering.
    if (w->isMinimized()) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (minimized)" << m_effect->getWindowId(w);
        if (rejectedOnlyBecauseMinimized) {
            *rejectedOnlyBecauseMinimized = true;
        }
        return false;
    }
    qCDebug(lcEffect) << "isEligibleForTilingNotify: accepted" << m_effect->getWindowId(w) << "size=" << frame.size()
                      << "class=" << w->windowClass() << "skipSwitcher=" << w->isSkipSwitcher()
                      << "keepAbove=" << w->keepAbove() << "transient=" << (w->transientFor() != nullptr);
    return true;
}

void TilingHandler::applyFloatCleanup(const QString& windowId)
{
    m_effect->m_navigationHandler->setWindowFloating(windowId, true);
    // A floating window is no longer tile-managed on any screen — clear tiled
    // tracking. clearWindowTiledAllScreens re-resolves the window's rules when the
    // tiled status flips, so a baseline border / title-bar rule scoped to tiled
    // windows stops drawing / hiding on the now-floating window (the setWindowFloating
    // above also re-resolves on the IsFloating flip; both coalesce).
    clearWindowTiledAllScreens(windowId);
    // Drop centering/target tracking too — a floated window isn't being
    // tiled anymore so a stale entry here would trigger centering on the
    // next frameGeometryChanged, snapping the floated window back into an
    // old zone rect. slotWindowsTileRequested no longer clears these
    // globally (it can't without wiping sibling-VS state), so the float
    // path has to clean up after itself.
    m_tileTargetZones.remove(windowId);
    m_centeredWaylandZones.remove(windowId);
    // Shared placement-flip funnel (update-or-remove in the same turn) —
    // the bare removal here left the float paths WITHOUT a bulk
    // updateAllDecorations follow-up (daemon auto-float past maxWindows)
    // undecorated until an unrelated refresh, the same drag-start blackout
    // the snap engine had. The tiled/floating facts were flipped above, so
    // the funnel resolves the floating-state chain.
    m_effect->reconcileDecorationOnPlacementFlip(windowId);
    unmaximizeMonocleWindow(windowId);
}

void TilingHandler::markWindowTiled(const QString& screenId, const QString& windowId)
{
    const bool wasTiled = isTiledWindow(windowId);
    TilingStateHelpers::addTiledOnScreen(m_border, screenId, windowId);
    // Re-resolve only on the false→true transition: a window already tiled on
    // another screen stays tiled, so re-adding it changes no rule outcome.
    if (!wasTiled) {
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
}

void TilingHandler::clearWindowTiledAllScreens(const QString& windowId)
{
    if (TilingStateHelpers::removeFromAllScreens(m_border, windowId)) {
        // Was tiled on at least one screen and now is not — IsTiled flipped.
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
}

void TilingHandler::clearWindowTiledOnScreen(const QString& screenId, const QString& windowId)
{
    if (TilingStateHelpers::removeTiledOnScreen(m_border, screenId, windowId) && !isTiledWindow(windowId)) {
        // Removed from this screen and not tiled on any other — IsTiled flipped.
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
}

} // namespace PlasmaZones
