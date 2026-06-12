// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../autotilehandler.h"
#include "../navigationhandler.h"
#include "../plasmazoneseffect.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effectwindow.h>
#include <window.h>

#include <QLoggingCategory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// ═══════════════════════════════════════════════════════════════════════════════
// Monocle helpers
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::unmaximizeMonocleWindow(const QString& windowId)
{
    if (!m_monocleMaximizedWindows.remove(windowId)) {
        return;
    }
    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
    if (!w) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    ++m_suppressMaximizeChanged;
    kw->maximize(KWin::MaximizeRestore);
    --m_suppressMaximizeChanged;
}

void AutotileHandler::restoreAllMonocleMaximized()
{
    if (m_monocleMaximizedWindows.isEmpty()) {
        return;
    }
    ++m_suppressMaximizeChanged;
    for (const QString& wid : std::as_const(m_monocleMaximizedWindows)) {
        KWin::EffectWindow* w = m_effect->findWindowById(wid);
        if (w) {
            KWin::Window* kw = w->window();
            if (kw) {
                kw->maximize(KWin::MaximizeRestore);
            }
        }
    }
    --m_suppressMaximizeChanged;
    m_monocleMaximizedWindows.clear();
}

void AutotileHandler::clearTiledTracking()
{
    // Bookkeeping only. Physical title-bar restores are the
    // DecorationManager's job — teardown callers pair this with
    // DecorationManager::restoreAll().
    m_border.tiledWindowsByScreen.clear();
}

bool AutotileHandler::updateHideTitleBarsSetting(bool enabled)
{
    if (m_border.hideTitleBars == enabled) {
        return false;
    }
    m_border.hideTitleBars = enabled;
    if (!enabled) {
        // Turning OFF — release every autotile ownership; the manager
        // restores each title bar no other owner still claims. Deferred +
        // an immediate drain: each restore is a 30-120 ms synchronous
        // Wayland round-trip, so the drain runs them one per event-loop
        // tick instead of stalling the compositor for the whole batch.
        m_effect->decorationManager()->releaseAllOfKind(DecorationManager::OwnerKind::Autotile,
                                                        DecorationManager::Restore::Deferred);
        m_effect->decorationManager()->drainPendingRestores();
    } else {
        // Turning ON — hide title bars for all currently tiled windows. The
        // windows are already placed in their zones, so the AlreadyPlaced
        // sequence re-asserts the zone rect across the decoration change
        // (KWin holds the client size constant, which would otherwise leave
        // a title-bar-height gap).
        const auto pairs = AutotileStateHelpers::allTiledPairs(m_border);
        for (const auto& p : pairs) {
            m_effect->decorationManager()->acquire(p.first, DecorationManager::autotile(p.second),
                                                   DecorationManager::Placement::AlreadyPlaced);
        }
    }
    return true;
}

bool AutotileHandler::updateShowBorderSetting(bool enabled)
{
    if (m_border.showBorder == enabled) {
        return false;
    }
    m_border.showBorder = enabled;
    return true;
}

void AutotileHandler::setFocusFollowsMouse(bool enabled)
{
    m_focusFollowsMouse = enabled;
}

bool AutotileHandler::saveAndRecordPreAutotileGeometry(const QString& windowId, const QString& screenId,
                                                       const QRectF& frame, bool knownFreeFloating)
{
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return false;
    }
    if (!frame.isValid() || frame.width() <= 0 || frame.height() <= 0) {
        return false;
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
        const auto screenIt = m_preAutotileGeometries.constFind(screenId);
        if (screenIt != m_preAutotileGeometries.constEnd() && screenIt->contains(windowId)) {
            return false;
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
    if (m_effect->isWindowMarkedSnapped(windowId)) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry for snap-managed window (frame is zone rect)" << windowId
                          << "on" << screenId;
        return true;
    }
    if (!knownFreeFloating && !m_effect->isWindowFloating(windowId)) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry for snapped window" << windowId << "on" << screenId;
        return true;
    }
    m_preAutotileGeometries[screenId][windowId] = frame;
    qCDebug(lcEffect) << "Saved pre-autotile geometry for" << windowId << "on" << screenId << ":" << frame;
    if (m_effect->m_daemonServiceRegistered) {
        // overwrite=true: the isWindowMarkedSnapped() and isWindowFloating() guards
        // above already skipped this path for snapped/tiled windows (the former even
        // on the knownFreeFloating fast path), so when we reach here the frame is the
        // window's authoritative free-floating geometry for THIS session.
        // The daemon persists pre-tile entries across window close/reopen
        // (keyed by appId for session restore), so a stale entry from a
        // prior session would otherwise block the fresh capture and leave
        // float-restore teleporting the window to ancient coordinates.
        PhosphorProtocol::ClientHelpers::fireAndForget(
            m_effect, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
            {windowId, static_cast<int>(frame.x()), static_cast<int>(frame.y()), static_cast<int>(frame.width()),
             static_cast<int>(frame.height()), screenId, true},
            QStringLiteral("storePreTileGeometry"));
    }
    return true;
}

bool AutotileHandler::isAutotileScreen(const QString& screenId) const
{
    return m_autotileScreens.contains(screenId);
}

void AutotileHandler::savePreAutotileForDesktopMove(const QString& windowId, const QString& screenId)
{
    // Preserve the window's pre-autotile geometry before onWindowClosed clears it.
    // When the window is re-added on the target desktop, this geometry is restored
    // so that float-restore returns to the original position, not the tiled frame.
    //
    // Stamped with the BUCKET's screen (not the caller's) so the restore
    // path can detect a cross-screen desktop move and decline a saved rect
    // from a different monitor's coordinate space. Scan all buckets: a VS
    // config change can re-resolve the window's screen without re-keying the
    // geometry bucket (same all-bucket policy as the desktop-switch Pass-2
    // scan and the cross-monitor snapshot).
    for (auto sgIt = m_preAutotileGeometries.constBegin(); sgIt != m_preAutotileGeometries.constEnd(); ++sgIt) {
        const QRectF rect = sgIt->value(windowId);
        if (rect.isValid()) {
            m_savedPreAutotileForDesktopMove[windowId] = {sgIt.key(), rect};
            qCDebug(lcEffect) << "Preserved pre-autotile geometry for desktop move:" << windowId << "bucket"
                              << sgIt.key() << "rect=" << rect;
            return;
        }
    }
}

bool AutotileHandler::isEligibleForAutotileNotify(KWin::EffectWindow* w) const
{
    // Early-out: KWin internal surfaces (overlay QQuickViews, zone overlays, etc.)
    // are never eligible for autotile notification. KWin's InternalWindow::minSize()
    // segfaults when the backing QWindow is null. See discussion #511.
    if (w && w->window() && w->window()->isInternal()) {
        qCDebug(lcEffect) << "isEligibleForAutotileNotify: rejected (internal window)" << m_effect->getWindowId(w);
        return false;
    }
    if (!w || !m_effect->shouldHandleWindow(w)) {
        qCDebug(lcEffect) << "isEligibleForAutotileNotify: rejected (not handleable)"
                          << (w ? m_effect->getWindowId(w) : QStringLiteral("null"));
        return false;
    }
    if (!m_effect->isTileableWindow(w)) {
        qCDebug(lcEffect) << "isEligibleForAutotileNotify: rejected (not tileable)" << m_effect->getWindowId(w);
        return false;
    }
    if (w->isMinimized()) {
        qCDebug(lcEffect) << "isEligibleForAutotileNotify: rejected (minimized)" << m_effect->getWindowId(w);
        return false;
    }
    if (!w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
        qCDebug(lcEffect) << "isEligibleForAutotileNotify: rejected (wrong desktop/activity)"
                          << m_effect->getWindowId(w);
        return false;
    }
    // Reject windows smaller than the user-configured minimum size.
    // Prevents small utility windows (emoji picker, color picker, etc.)
    // from entering the tiling tree and disrupting the layout.
    const QRectF frame = w->frameGeometry();
    if ((m_effect->m_cachedMinWindowWidth > 0 && frame.width() < m_effect->m_cachedMinWindowWidth)
        || (m_effect->m_cachedMinWindowHeight > 0 && frame.height() < m_effect->m_cachedMinWindowHeight)) {
        qCDebug(lcEffect) << "isEligibleForAutotileNotify: rejected (too small)" << m_effect->getWindowId(w)
                          << "size=" << frame.size() << "threshold=" << m_effect->m_cachedMinWindowWidth << "x"
                          << m_effect->m_cachedMinWindowHeight;
        return false;
    }
    qCDebug(lcEffect) << "isEligibleForAutotileNotify: accepted" << m_effect->getWindowId(w) << "size=" << frame.size()
                      << "class=" << w->windowClass() << "skipSwitcher=" << w->isSkipSwitcher()
                      << "keepAbove=" << w->keepAbove() << "transient=" << (w->transientFor() != nullptr);
    return true;
}

void AutotileHandler::applyFloatCleanup(const QString& windowId)
{
    m_effect->m_navigationHandler->setWindowFloating(windowId, true);
    // A floating window is no longer tile-managed on any screen — release
    // autotile's decoration ownership (the manager restores the title bar
    // unless another owner still claims it) and clear tiled tracking.
    m_effect->decorationManager()->releaseKind(windowId, DecorationManager::OwnerKind::Autotile);
    AutotileStateHelpers::removeFromAllScreens(m_border, windowId);
    // Drop centering/target tracking too — a floated window isn't being
    // tiled anymore so a stale entry here would trigger centering on the
    // next frameGeometryChanged, snapping the floated window back into an
    // old zone rect. slotWindowsTileRequested no longer clears these
    // globally (it can't without wiping sibling-VS state), so the float
    // path has to clean up after itself.
    m_autotileTargetZones.remove(windowId);
    m_centeredWaylandZones.remove(windowId);
    m_effect->removeWindowBorder(windowId);
    unmaximizeMonocleWindow(windowId);
}

} // namespace PlasmaZones
