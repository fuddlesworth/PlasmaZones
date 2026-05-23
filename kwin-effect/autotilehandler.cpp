// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotilehandler.h"
#include "plasmazoneseffect.h"
#include "windowanimator.h"
#include "navigationhandler.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/WindowMarshalling.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QTimer>
#include <QtMath>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

AutotileHandler::AutotileHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void AutotileHandler::handleCursorMoved(const QPointF& pos, const QString& screenId)
{
    if (!m_focusFollowsMouse || m_autotileScreens.isEmpty()) {
        return;
    }

    // Only act on autotile screens (screenId already resolved by caller)
    if (screenId.isEmpty() || !m_autotileScreens.contains(screenId)) {
        return;
    }

    // Find the topmost autotile-managed window under the cursor.
    // Iterate stacking order in reverse (top → bottom).
    const auto windows = KWin::effects->stackingOrder();
    for (int i = windows.size() - 1; i >= 0; --i) {
        KWin::EffectWindow* w = windows[i];
        if (!w || w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
            continue;
        }
        // Geometry check first (cheap QRectF::contains) before shouldHandleWindow (allocates via windowClass())
        if (!w->frameGeometry().contains(pos)) {
            continue;
        }
        // Look through our own overlay/editor layer-shell surfaces — they are
        // full-screen and always topmost on the autotile monitor, so the bail
        // below would otherwise kill FFM forever (discussion #461 #3).
        if (PlasmaZonesEffect::isOwnOverlayClass(w->windowClass())) {
            continue;
        }
        // A non-autotile window (excluded app, keep-above overlay, popup, dialog,
        // Spectacle, etc.) occludes the cursor — don't look through it to focus a
        // tiled window beneath. This prevents focus-stealing from emoji pickers,
        // screenshot tools, and other excluded/overlay windows.
        if (!m_effect->isTileableWindow(w) || !m_effect->shouldHandleWindow(w)) {
            return;
        }
        // Also block focus for windows below the minimum size threshold.
        // These are normal windows (pass isTileableWindow) but too small
        // for autotile — e.g., emoji picker, small utilities. Without this,
        // hovering over them triggers auto-focus even though they're not tiled.
        {
            const QRectF frame = w->frameGeometry();
            if ((m_effect->m_cachedMinWindowWidth > 0 && frame.width() < m_effect->m_cachedMinWindowWidth)
                || (m_effect->m_cachedMinWindowHeight > 0 && frame.height() < m_effect->m_cachedMinWindowHeight)) {
                return;
            }
        }
        // Skip the activateWindow call when the window under the cursor
        // already holds compositor focus. The live activeWindow() read is
        // load-bearing: a local "last auto-focused window" cache would go
        // stale every time focus moved through another path (keyboard
        // shortcut, click, daemon-driven activate, focus-stealing window),
        // and the next cursor pass over the originally-cached window would
        // short-circuit without re-focusing it. See discussion #461 item 13.
        if (w == KWin::effects->activeWindow()) {
            return; // Already focused — no-op
        }
        // Only focus windows on autotile screens
        if (!m_autotileScreens.contains(m_effect->getWindowScreenId(w))) {
            return;
        }
        KWin::effects->activateWindow(w);
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Geometry persistence
// ═══════════════════════════════════════════════════════════════════════════════
// Integration points
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileHandler::notifyWindowAdded(KWin::EffectWindow* w)
{
    if (!isEligibleForAutotileNotify(w)) {
        return false;
    }

    const QString windowId = m_effect->getWindowId(w);

    // Window was already closed before we could notify open — skip (D-Bus ordering race)
    if (m_pendingCloses.remove(windowId)) {
        return false;
    }

    if (m_notifiedWindows.contains(windowId)) {
        return false;
    }
    m_notifiedWindows.insert(windowId);

    QString screenId = m_effect->getWindowScreenId(w);
    m_notifiedWindowScreens[windowId] = screenId;

    // Only notify autotile daemon for windows on autotile screens
    if (m_autotileScreens.contains(screenId)) {
        // Save pre-autotile geometry BEFORE the daemon tiles the window.
        // Without this, a window launched directly into autotile has no saved
        // geometry — floating it would leave it at its tiled position instead
        // of restoring to its original free-floating size.
        //
        // knownFreeFloating=true: this is the window-opened path, so the frame
        // is KWin's spawn geometry — the authoritative pre-autotile position.
        // The FloatingCache is not yet populated for fresh windows, so without
        // this flag the isWindowFloating() guard would drop the one-shot save.
        saveAndRecordPreAutotileGeometry(windowId, screenId, w->frameGeometry(),
                                         /*knownFreeFloating=*/true);

        int minWidth = 0;
        int minHeight = 0;
        KWin::Window* kw = w->window();
        // Internal windows (our own overlays) crash on minSize(); see discussion #511.
        if (kw && !kw->isInternal()) {
            const QSizeF minSize = kw->minSize();
            if (minSize.isValid()) {
                minWidth = qCeil(minSize.width());
                minHeight = qCeil(minSize.height());
            }
        }

        auto* watcher =
            new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                            PhosphorProtocol::Service::Interface::Autotile,
                                            QStringLiteral("windowOpened"), {windowId, screenId, minWidth, minHeight}),
                                        this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            if (w->isError()) {
                qCWarning(lcEffect) << "windowOpened D-Bus call failed for" << windowId << ":" << w->error().message();
                m_notifiedWindows.remove(windowId);
                m_notifiedWindowScreens.remove(windowId);
                // notifyWindowAdded() returned true on the synchronous
                // path, so the caller (PlasmaZonesEffect::slotWindowAdded)
                // left first-frame open suppression engaged expecting a
                // moveResize from the daemon's tile decision. The D-Bus
                // call failed — no moveResize is coming — so release
                // suppression here rather than letting the window sit
                // invisible until the 250 ms deadline.
                if (KWin::EffectWindow* effectWindow = m_effect->findWindowById(windowId)) {
                    m_effect->endRestoreSuppression(effectWindow);
                }
            }
        });
        qCDebug(lcEffect) << "Notified autotile: windowOpened" << windowId << "on screen" << screenId
                          << "minSize:" << minWidth << "x" << minHeight;
        return true;
    }
    return false;
}

void AutotileHandler::notifyWindowsAddedBatch(const QList<KWin::EffectWindow*>& windows,
                                              const QSet<QString>& screenFilter, bool resetNotified)
{
    // Collect eligible windows using the same filtering as notifyWindowAdded,
    // then send one batch D-Bus call instead of per-window round-trips.
    PhosphorProtocol::WindowOpenedList batchEntries;
    QStringList batchWindowIds; // for error rollback

    for (KWin::EffectWindow* w : windows) {
        if (!isEligibleForAutotileNotify(w)) {
            continue;
        }

        const QString screenId = m_effect->getWindowScreenId(w);
        if (!screenFilter.isEmpty() && !screenFilter.contains(screenId)) {
            continue;
        }
        if (!m_autotileScreens.contains(screenId)) {
            continue;
        }

        const QString windowId = m_effect->getWindowId(w);

        if (m_pendingCloses.remove(windowId)) {
            continue;
        }

        if (resetNotified) {
            m_notifiedWindows.remove(windowId);
        }
        if (m_notifiedWindows.contains(windowId)) {
            continue;
        }
        m_notifiedWindows.insert(windowId);
        m_notifiedWindowScreens[windowId] = screenId;

        // knownFreeFloating=true: window-opened path — see notifyWindowAdded()
        // for rationale. Fresh windows aren't in the FloatingCache yet.
        saveAndRecordPreAutotileGeometry(windowId, screenId, w->frameGeometry(),
                                         /*knownFreeFloating=*/true);

        int minWidth = 0;
        int minHeight = 0;
        KWin::Window* kw = w->window();
        // Internal windows (our own overlays) crash on minSize(); see discussion #511.
        if (kw && !kw->isInternal()) {
            const QSizeF minSize = kw->minSize();
            if (minSize.isValid()) {
                minWidth = qCeil(minSize.width());
                minHeight = qCeil(minSize.height());
            }
        }

        PhosphorProtocol::WindowOpenedEntry entry;
        entry.windowId = windowId;
        entry.screenId = screenId;
        entry.minWidth = minWidth;
        entry.minHeight = minHeight;
        batchEntries.append(entry);
        batchWindowIds.append(windowId);
    }

    if (batchEntries.isEmpty()) {
        return;
    }

    auto* watcher =
        new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                        PhosphorProtocol::Service::Interface::Autotile,
                                        QStringLiteral("windowsOpenedBatch"), {QVariant::fromValue(batchEntries)}),
                                    this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, batchWindowIds](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lcEffect) << "windowsOpenedBatch D-Bus call failed:" << w->error().message();
            for (const QString& wid : batchWindowIds) {
                m_notifiedWindows.remove(wid);
                m_notifiedWindowScreens.remove(wid);
            }
        }
    });
    qCInfo(lcEffect) << "Notified autotile: windowsOpenedBatch with" << batchEntries.size() << "windows";
}

void AutotileHandler::handleWindowOutputChanged(KWin::EffectWindow* w)
{
    if (!w || w->isDeleted() || m_inOutputChanged) {
        return;
    }
    // Re-entrancy guard: geometry changes from onWindowClosed/notifyWindowAdded
    // could trigger outputChanged again if tiling moves the window across screens.
    m_inOutputChanged = true;
    const auto guard = qScopeGuard([this] {
        m_inOutputChanged = false;
    });

    const QString windowId = m_effect->getWindowId(w);
    const QString newScreenId = m_effect->getWindowScreenId(w);

    if (!m_notifiedWindows.contains(windowId)) {
        // Window not tracked — but if it moved TO an autotile screen, add it
        if (m_autotileScreens.contains(newScreenId) && m_effect->shouldHandleWindow(w) && !w->isMinimized()
            && w->isOnCurrentDesktop() && w->isOnCurrentActivity()) {
            notifyWindowAdded(w);
            m_effect->updateAllBorders();
        }
        return;
    }

    const QString oldScreenId = m_notifiedWindowScreens.value(windowId);

    if (oldScreenId.isEmpty() || oldScreenId == newScreenId) {
        return; // Same screen or unknown — no transfer needed
    }

    const bool oldIsAutotile = m_autotileScreens.contains(oldScreenId);
    const bool newIsAutotile = m_autotileScreens.contains(newScreenId);

    if (!oldIsAutotile && !newIsAutotile) {
        return; // Neither screen is autotiled — snapping unsnap handled by effect's outputChanged
    }

    qCInfo(lcEffect) << "Window moved between monitors:" << windowId << oldScreenId << "->" << newScreenId;

    // Snapshot the pre-autotile geometry BEFORE onWindowClosed clears it.
    // onWindowClosed removes m_preAutotileGeometries AND m_savedPreAutotileForDesktopMove,
    // so we must hold onto the geometry locally.
    QRectF savedPreAutotileGeo;
    if (oldIsAutotile && m_preAutotileGeometries.contains(oldScreenId)) {
        const auto& screenGeometries = m_preAutotileGeometries[oldScreenId];
        const QString savedKey = AutotileStateHelpers::findSavedGeometryKey(screenGeometries, windowId);
        if (!savedKey.isEmpty()) {
            savedPreAutotileGeo = screenGeometries.value(savedKey);
        }
    }

    // Restore title bar before removing
    if (isBorderlessWindow(windowId)) {
        KWin::Window* kw = w->window();
        if (kw) {
            kw->setNoBorder(false);
        }
    }

    // Remove from old screen's autotile state
    onWindowClosed(windowId, oldScreenId);

    if (newIsAutotile && !w->isMinimized() && w->isOnCurrentDesktop() && w->isOnCurrentActivity()) {
        // Re-add on new autotile screen, carrying over pre-autotile geometry.
        // Cancel any pending cross-screen restore — the window is back in autotile.
        auto pendingIt = m_pendingCrossScreenRestore.find(windowId);
        if (pendingIt != m_pendingCrossScreenRestore.end()) {
            QObject::disconnect(pendingIt.value());
            m_pendingCrossScreenRestore.erase(pendingIt);
        }
        if (savedPreAutotileGeo.isValid()) {
            m_preAutotileGeometries[newScreenId][windowId] = savedPreAutotileGeo;
        }
        notifyWindowAdded(w);
    } else if (oldIsAutotile && !newIsAutotile) {
        // Autotile → snapping: restore the window's original (pre-snap/pre-tile)
        // SIZE after the drag ends.  The effect-side m_preAutotileGeometries may
        // hold the snap zone geometry (if the window was snapped before entering
        // autotile), so we ask the daemon for the true pre-tile geometry instead.
        // If unavailable, fall back to the effect-side cache.
        QPointer<KWin::EffectWindow> safeW = w;
        const QString wid = windowId;

        // Cancel any prior pending restore for this window (rapid back-and-forth)
        auto oldIt = m_pendingCrossScreenRestore.find(windowId);
        if (oldIt != m_pendingCrossScreenRestore.end()) {
            QObject::disconnect(oldIt.value());
            m_pendingCrossScreenRestore.erase(oldIt);
        }

        // Fetch the daemon's pre-tile geometry (original size before any snapping).
        // The effect-side savedPreAutotileGeo is kept as fallback.
        const int fallbackW = savedPreAutotileGeo.isValid() ? std::max(0, qRound(savedPreAutotileGeo.width())) : 0;
        const int fallbackH = savedPreAutotileGeo.isValid() ? std::max(0, qRound(savedPreAutotileGeo.height())) : 0;

        auto* watcher = new QDBusPendingCallWatcher(
            PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("getValidatedPreTileGeometry"), {windowId}),
            m_effect);
        connect(watcher, &QDBusPendingCallWatcher::finished, m_effect,
                [this, safeW, wid, fallbackW, fallbackH](QDBusPendingCallWatcher* pw) {
                    pw->deleteLater();

                    int restoreW = fallbackW;
                    int restoreH = fallbackH;
                    QDBusPendingReply<bool, int, int, int, int> reply = *pw;
                    if (reply.isValid() && reply.count() >= 5 && reply.argumentAt<0>()) {
                        int dw = reply.argumentAt<3>();
                        int dh = reply.argumentAt<4>();
                        if (dw > 0 && dh > 0) {
                            restoreW = dw;
                            restoreH = dh;
                        }
                    }

                    if (restoreW <= 0 || restoreH <= 0 || !safeW || safeW->isDeleted()) {
                        return;
                    }

                    // If the window bounced back to an autotile screen during the
                    // D-Bus round-trip, skip the restore — it's being tiled again.
                    const QString currentScreen = m_effect->getWindowScreenId(safeW);
                    if (m_autotileScreens.contains(currentScreen)) {
                        return;
                    }

                    // If the drag already ended, apply immediately — unless the
                    // window was snapped to a zone by dragStopped during this D-Bus
                    // round-trip. In that case, zone geometry is already correct.
                    if (!safeW->isUserMove() && !safeW->isUserResize()) {
                        if (!m_effect->isWindowFloating(wid)) {
                            return; // Snapped to zone — don't clobber zone geometry
                        }
                        const QRectF frame = safeW->frameGeometry();
                        const QRect geo(qRound(frame.x()), qRound(frame.y()), restoreW, restoreH);
                        m_effect->applySnapGeometry(safeW, geo);
                        return;
                    }

                    // Still dragging — wait for drop, then restore size once.
                    // Use a shared connection so the lambda can disconnect itself
                    // after firing once — preventing subsequent user resizes from
                    // snapping the window back to the pre-autotile size.
                    auto sharedConn = std::make_shared<QMetaObject::Connection>();
                    *sharedConn = connect(safeW.data(), &KWin::EffectWindow::windowFinishUserMovedResized, m_effect,
                                          [this, safeW, wid, restoreW, restoreH, sharedConn](KWin::EffectWindow*) {
                                              // Disconnect immediately so this only fires once (the drop),
                                              // not on every subsequent user resize.
                                              QObject::disconnect(*sharedConn);
                                              m_pendingCrossScreenRestore.remove(wid);
                                              if (!safeW || safeW->isDeleted()) {
                                                  return;
                                              }
                                              // Guard: window may have bounced back to autotile during drag.
                                              const QString dropScreen = m_effect->getWindowScreenId(safeW);
                                              if (m_autotileScreens.contains(dropScreen)) {
                                                  return;
                                              }
                                              // Guard: window may have been snapped to a zone by dragStopped.
                                              if (!m_effect->isWindowFloating(wid)) {
                                                  return;
                                              }
                                              const QRectF frame = safeW->frameGeometry();
                                              const QRect geo(qRound(frame.x()), qRound(frame.y()), restoreW, restoreH);
                                              m_effect->applySnapGeometry(safeW, geo);
                                          });
                    m_pendingCrossScreenRestore[wid] = *sharedConn;
                });

        // NOTE: Do NOT call windowUnsnapped here. The drag-drop handler
        // (WindowDragAdaptor::dragStopped) manages the zone assignment transition.
        // Firing windowUnsnapped now would race with the snap-to-zone D-Bus call
        // from the drop handler, destroying the zone assignment that was just created.
        // The drop handler's cross-screen path already clears stale state.

        // Raise above existing windows so it doesn't end up buried behind
        // snapped windows on the target screen.
        KWin::Window* kw = w->window();
        if (kw) {
            auto* ws = KWin::Workspace::self();
            if (ws) {
                ws->raiseWindow(kw);
            }
        }
    }

    m_effect->updateAllBorders();
}

void AutotileHandler::onWindowClosed(const QString& windowId, const QString& screenId)
{
    // If we haven't notified the daemon about this window yet, record the close
    // so we can suppress the open if it arrives late (D-Bus ordering race)
    if (!m_notifiedWindows.contains(windowId) && m_autotileScreens.contains(screenId)) {
        m_pendingCloses.insert(windowId);
    }

    // Delegate compositor-agnostic state cleanup to the shared helper
    AutotileStateHelpers::AutotileWindowState windowState{
        m_notifiedWindows,      m_notifiedWindowScreens,   m_minimizeFloatedWindows, m_autotileTargetZones,
        m_centeredWaylandZones, m_monocleMaximizedWindows, m_preAutotileGeometries};
    AutotileStateHelpers::cleanupClosedWindowState(windowId, screenId, m_border, windowState);
    // Cancel any pending debounced minimize→float commit — it must not fire
    // against a destroyed window.
    cancelPendingMinimizeFloat(windowId);

    // KWin-specific cleanup not covered by the shared helper
    m_savedNotifiedForDesktopReturn.remove(windowId);
    m_savedPreAutotileForDesktopMove.remove(windowId);
    auto pendingConn = m_pendingCrossScreenRestore.find(windowId);
    if (pendingConn != m_pendingCrossScreenRestore.end()) {
        QObject::disconnect(pendingConn.value());
        m_pendingCrossScreenRestore.erase(pendingConn);
    }
    // Remove from saved stacking orders so stale IDs don't accumulate
    if (m_savedSnapStackingOrder.contains(screenId)) {
        m_savedSnapStackingOrder[screenId].removeAll(windowId);
    }
    if (m_savedAutotileStackingOrder.contains(screenId)) {
        m_savedAutotileStackingOrder[screenId].removeAll(windowId);
    }

    // Notify autotile daemon
    if (m_autotileScreens.contains(screenId)) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Autotile,
                                                       QStringLiteral("windowClosed"), {windowId},
                                                       QStringLiteral("windowClosed"));
        qCDebug(lcEffect) << "Notified autotile: windowClosed" << windowId << "on screen" << screenId;
    }
}

void AutotileHandler::handleDragToFloat(KWin::EffectWindow* w, const QString& windowId, const QString& screenId,
                                        bool immediate)
{
    // Restore border and clear tiling state synchronously — don't wait for
    // the daemon's async windowFloatingChanged signal, which may never arrive
    // (e.g., cross-screen drag where onWindowClosed removes daemon tracking
    // before setWindowFloatingForScreen processes).
    applyFloatCleanup(windowId);

    // Restore pre-autotile SIZE at the window's current position.
    if (w) {
        auto screenIt = m_preAutotileGeometries.constFind(screenId);
        if (screenIt != m_preAutotileGeometries.constEnd()) {
            const QString geoKey = AutotileStateHelpers::findSavedGeometryKey(screenIt.value(), windowId);
            if (!geoKey.isEmpty()) {
                const QRectF savedGeo = screenIt.value().value(geoKey);
                if (savedGeo.isValid()) {
                    const int savedW = qRound(savedGeo.width());
                    const int savedH = qRound(savedGeo.height());

                    if (immediate) {
                        // Drag-start path: apply synchronously during the
                        // interactive move (allowDuringDrag=true) so the user
                        // sees the window return to its free-floating size the
                        // moment they start dragging — matches snap-mode behavior.
                        // Re-center horizontally under the cursor so the window
                        // doesn't "jump away" from the grab point when it shrinks.
                        QRectF currentFrame = w->frameGeometry();
                        const QPointF cursor = KWin::effects->cursorPos();
                        int newX = qRound(currentFrame.x());
                        int newY = qRound(currentFrame.y());
                        if (currentFrame.width() > 0 && savedW < currentFrame.width()) {
                            const qreal cursorOffsetRatio = (cursor.x() - currentFrame.x()) / currentFrame.width();
                            newX = qRound(cursor.x() - cursorOffsetRatio * savedW);
                        }
                        QRect sizeRestored(newX, newY, savedW, savedH);
                        m_effect->applySnapGeometry(w, sizeRestored, /*allowDuringDrag=*/true);
                        qCInfo(lcEffect) << "Drag-start float: restored pre-autotile size for" << windowId << savedW
                                         << "x" << savedH;
                    } else {
                        // Drag-stop path: defer to next event loop tick so
                        // KWin has finished the interactive move and the window's
                        // frame geometry reflects the actual drop position.
                        QPointer<KWin::EffectWindow> wp = w;
                        PlasmaZonesEffect* effect = m_effect;
                        QTimer::singleShot(0, effect, [effect, wp, windowId, savedW, savedH]() {
                            if (!wp || wp->isDeleted()) {
                                return;
                            }
                            // Skip if the window was re-snapped during the deferred tick
                            // (e.g., dropped on a zone on a snap screen during cross-VS drag).
                            if (!effect->isWindowFloating(effect->getWindowId(wp))) {
                                qCDebug(lcEffect)
                                    << "Drag-to-float: skipping size restore for re-snapped window" << windowId;
                                return;
                            }
                            QRectF currentFrame = wp->frameGeometry();
                            QRect sizeRestored(qRound(currentFrame.x()), qRound(currentFrame.y()), savedW, savedH);
                            effect->applySnapGeometry(wp, sizeRestored);
                            qCInfo(lcEffect) << "Drag-to-float: restored pre-autotile size for" << windowId << savedW
                                             << "x" << savedH;
                        });
                    }
                }
            }
        }
    }

    m_effect->updateAllBorders();
}

void AutotileHandler::onDaemonReady()
{
    loadSettings();
    connectSignals();
    m_notifiedWindows.clear();
    m_notifiedWindowScreens.clear();
    m_savedNotifiedForDesktopReturn.clear();
    m_savedPreAutotileForDesktopMove.clear();
    m_pendingCloses.clear();
}

// handleAutotileFloatToggle removed: float toggle is now routed through
// the unified WTA toggleFloatForWindow method via slotToggleWindowFloatRequested.

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus signal connections and settings
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::connectSignals()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    // Disconnect first so daemon restarts don't accumulate duplicate match
    // rules. Qt's QDBusConnection::connect can register the same handler
    // twice if called twice with identical args, which would cause each
    // signal to invoke the slot N times after N daemon restarts.
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("windowsTileRequested"), this,
                   SLOT(slotWindowsTileRequested(PhosphorProtocol::TileRequestList)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("focusWindowRequested"), this,
                   SLOT(slotFocusWindowRequested(QString)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("enabledChanged"), this,
                   SLOT(slotEnabledChanged(bool)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("autotileScreensChanged"), this,
                   SLOT(slotScreensChanged(QStringList, bool)));
    bus.disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                   PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("windowFloatingChanged"), this,
                   SLOT(slotWindowFloatingChanged(QString, bool, QString)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("windowsTileRequested"), this,
                SLOT(slotWindowsTileRequested(PhosphorProtocol::TileRequestList)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("focusWindowRequested"), this,
                SLOT(slotFocusWindowRequested(QString)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("enabledChanged"), this,
                SLOT(slotEnabledChanged(bool)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("autotileScreensChanged"), this,
                SLOT(slotScreensChanged(QStringList, bool)));

    bus.connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::Autotile, QStringLiteral("windowFloatingChanged"), this,
                SLOT(slotWindowFloatingChanged(QString, bool, QString)));

    qCInfo(lcEffect) << "Connected to autotile D-Bus signals";
}

void AutotileHandler::loadSettings()
{
    // Query initial autotile screen set from daemon asynchronously. The
    // foreign org.freedesktop.DBus.Properties interface is correct for D-Bus
    // property access; ClientHelpers can't be used here because it hard-wires
    // the org.plasmazones interface. Bound by SyncCallTimeoutMs so a wedged
    // daemon doesn't leak a watcher for Qt's default 25 s.
    QDBusMessage msg =
        QDBusMessage::createMethodCall(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << PhosphorProtocol::Service::Interface::Autotile << QStringLiteral("autotileScreens");

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SyncCallTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isValid()) {
            QStringList screens = reply.value().variant().toStringList();
            const QSet<QString> added(screens.begin(), screens.end());
            m_autotileScreens = added;
            qCInfo(lcEffect) << "Loaded autotile screens:" << m_autotileScreens;

            if (!added.isEmpty()) {
                const auto windows = KWin::effects->stackingOrder();
                // Batch-notify all windows on autotile screens in one D-Bus call
                // instead of per-window windowOpened round-trips.
                notifyWindowsAddedBatch(windows, added, /*resetNotified=*/true);
            }
        } else {
            qCDebug(lcEffect) << "Autotile screens: query failed, daemon may not be running";
        }
    });
}

} // namespace PlasmaZones
