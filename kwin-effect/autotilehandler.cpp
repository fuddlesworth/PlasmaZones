// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotilehandler.h"
#include "plasmazoneseffect.h"
#include "windowanimator.h"
#include "navigationhandler.h"

#include <dbus_constants.h>
#include <dbus_helpers.h>
#include <dbus_types.h>

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

// ═══════════════════════════════════════════════════════════════════════════════
bool AutotileHandler::transferPreAutotileGeometry(const QString& windowId, const QString& fromScreenId,
                                                  const QString& toScreenId)
{
    auto fromIt = m_preAutotileGeometries.find(fromScreenId);
    if (fromIt == m_preAutotileGeometries.end()) {
        return false;
    }
    const QString savedKey = AutotileStateHelpers::findSavedGeometryKey(fromIt.value(), windowId);
    if (savedKey.isEmpty()) {
        return false;
    }
    QRectF geo = fromIt->take(savedKey);
    if (fromIt->isEmpty()) {
        m_preAutotileGeometries.erase(fromIt);
    }
    if (!geo.isValid()) {
        return false;
    }
    m_preAutotileGeometries[toScreenId][savedKey] = geo;
    return true;
}

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

void AutotileHandler::restoreAllBorderless()
{
    if (!m_border.borderlessWindows.isEmpty()) {
        const QSet<QString> toRestore = m_border.borderlessWindows;
        for (const QString& windowId : toRestore) {
            KWin::EffectWindow* w = m_effect->findWindowById(windowId);
            if (w) {
                setWindowBorderless(w, windowId, false);
            }
        }
    }
    // Clear all border tracking (orphans included)
    m_border.borderlessWindows.clear();
    m_border.tiledWindows.clear();
    m_border.zoneGeometries.clear();
}

void AutotileHandler::updateHideTitleBarsSetting(bool enabled)
{
    const bool wasEnabled = m_border.hideTitleBars;
    m_border.hideTitleBars = enabled;
    if (wasEnabled && !enabled) {
        // Turning OFF — restore title bars for all borderless windows
        m_border.zoneGeometries.clear();
        const QSet<QString> toRestore = m_border.borderlessWindows;
        for (const QString& windowId : toRestore) {
            KWin::EffectWindow* win = m_effect->findWindowById(windowId);
            if (win) {
                setWindowBorderless(win, windowId, false);
            }
        }
    } else if (!wasEnabled && enabled) {
        // Turning ON — hide title bars for all currently tiled windows
        const QSet<QString> tiled = m_border.tiledWindows;
        for (const QString& windowId : tiled) {
            KWin::EffectWindow* win = m_effect->findWindowById(windowId);
            if (win) {
                setWindowBorderless(win, windowId, true);
            }
        }
    }
}

void AutotileHandler::updateShowBorderSetting(bool enabled)
{
    m_border.showBorder = enabled;
}

void AutotileHandler::setFocusFollowsMouse(bool enabled)
{
    m_focusFollowsMouse = enabled;
    if (!enabled) {
        m_lastFocusFollowsMouseWindowId.clear();
    }
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
        const QString windowId = m_effect->getWindowId(w);
        if (windowId == m_lastFocusFollowsMouseWindowId) {
            return; // Already focused — no-op
        }
        // Only focus windows on autotile screens
        if (!m_autotileScreens.contains(m_effect->getWindowScreenId(w))) {
            return;
        }
        m_lastFocusFollowsMouseWindowId = windowId;
        KWin::effects->activateWindow(w);
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Geometry persistence
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileHandler::saveAndRecordPreAutotileGeometry(const QString& windowId, const QString& screenId,
                                                       const QRectF& frame)
{
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return false;
    }
    if (!frame.isValid() || frame.width() <= 0 || frame.height() <= 0) {
        return false;
    }
    auto& screenGeometries = m_preAutotileGeometries[screenId];
    // Use EXACT windowId match only — NOT stableId fallback.
    // Multiple instances of the same app (e.g., 3 Dolphin windows) share a stableId.
    // hasSavedGeometryForWindow's stableId fallback would return true after the first
    // instance is saved, preventing all other instances from saving their own geometry.
    // On restore, all instances would get the first instance's geometry — scrambling
    // window positions on every autotile ↔ snapping toggle.
    if (screenGeometries.contains(windowId)) {
        return false;
    }
    // Only save geometry for floating windows — snapped/tiled windows have zone
    // dimensions in frameGeometry(), not the original free-floating size. Storing
    // zone geometry here would cause handleDragToFloat to restore to zone size.
    if (!m_effect->isWindowFloating(windowId)) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry for snapped window" << windowId << "on" << screenId;
        return true;
    }
    screenGeometries[windowId] = frame;
    qCDebug(lcEffect) << "Saved pre-autotile geometry for" << windowId << "on" << screenId << ":" << frame;
    if (m_effect->m_daemonServiceRegistered) {
        // Use overwrite=false so a pre-existing snap geometry is preserved when
        // autotile activates on a screen with already-snapped windows. The effect-
        // local cache (screenGeometries) already guards against redundant stores
        // within an autotile session; overwrite=false prevents cross-mode clobbering.
        // Re-resolve screen ID (EDID-based) for daemon tracking D-Bus calls;
        // the screenId parameter may already be correct from callers that use
        // getWindowScreenId(), but re-resolve to be safe.
        QString resolvedScreenId = m_effect->getWindowScreenId(m_effect->findWindowById(windowId));
        DBusHelpers::fireAndForget(m_effect, DBus::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
                                   {windowId, static_cast<int>(frame.x()), static_cast<int>(frame.y()),
                                    static_cast<int>(frame.width()), static_cast<int>(frame.height()),
                                    resolvedScreenId.isEmpty() ? screenId : resolvedScreenId, false},
                                   QStringLiteral("storePreTileGeometry"));
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Borderless / title bar management
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::setWindowBorderless(KWin::EffectWindow* w, const QString& windowId, bool borderless)
{
    if (!w) {
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return;
    }
    if (borderless) {
        // Skip CSD windows (GTK/Electron) — hasDecoration() is false for them.
        if (!w->hasDecoration()) {
            return;
        }
        if (!m_border.borderlessWindows.contains(windowId)) {
            kw->setNoBorder(true);
            m_border.borderlessWindows.insert(windowId);
            qCDebug(lcEffect) << "Autotile: hid title bar for" << windowId;
        }
    } else {
        if (m_border.borderlessWindows.remove(windowId)) {
            m_border.zoneGeometries.remove(windowId);
            kw->setNoBorder(false);
            qCDebug(lcEffect) << "Autotile: restored title bar for" << windowId;
            m_effect->removeWindowBorder(windowId);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen accessors
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileHandler::isAutotileScreen(const QString& screenId) const
{
    return m_autotileScreens.contains(screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration points
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::notifyWindowAdded(KWin::EffectWindow* w)
{
    if (!isEligibleForAutotileNotify(w)) {
        return;
    }

    const QString windowId = m_effect->getWindowId(w);

    // Window was already closed before we could notify open — skip (D-Bus ordering race)
    if (m_pendingCloses.remove(windowId)) {
        return;
    }

    if (m_notifiedWindows.contains(windowId)) {
        return;
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
        saveAndRecordPreAutotileGeometry(windowId, screenId, w->frameGeometry());

        int minWidth = 0;
        int minHeight = 0;
        KWin::Window* kw = w->window();
        if (kw) {
            const QSizeF minSize = kw->minSize();
            if (minSize.isValid()) {
                minWidth = qCeil(minSize.width());
                minHeight = qCeil(minSize.height());
            }
        }

        QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath,
                                                          DBus::Interface::Autotile, QStringLiteral("windowOpened"));
        msg << windowId;
        msg << screenId;
        msg << minWidth;
        msg << minHeight;

        QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
        auto* watcher = new QDBusPendingCallWatcher(pending, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            if (w->isError()) {
                qCWarning(lcEffect) << "windowOpened D-Bus call failed for" << windowId << ":" << w->error().message();
                m_notifiedWindows.remove(windowId);
                m_notifiedWindowScreens.remove(windowId);
            }
        });
        qCDebug(lcEffect) << "Notified autotile: windowOpened" << windowId << "on screen" << screenId
                          << "minSize:" << minWidth << "x" << minHeight;
    }
}

void AutotileHandler::notifyWindowsAddedBatch(const QList<KWin::EffectWindow*>& windows,
                                              const QSet<QString>& screenFilter, bool resetNotified)
{
    // Collect eligible windows using the same filtering as notifyWindowAdded,
    // then send one batch D-Bus call instead of per-window round-trips.
    WindowOpenedList batchEntries;
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

        saveAndRecordPreAutotileGeometry(windowId, screenId, w->frameGeometry());

        int minWidth = 0;
        int minHeight = 0;
        KWin::Window* kw = w->window();
        if (kw) {
            const QSizeF minSize = kw->minSize();
            if (minSize.isValid()) {
                minWidth = qCeil(minSize.width());
                minHeight = qCeil(minSize.height());
            }
        }

        WindowOpenedEntry entry;
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

    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile,
                                                      QStringLiteral("windowsOpenedBatch"));
    msg << QVariant::fromValue(batchEntries);

    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
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

        QDBusMessage msg =
            QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                           QStringLiteral("getValidatedPreTileGeometry"));
        msg << windowId;
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), m_effect);
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

void AutotileHandler::savePreAutotileForDesktopMove(const QString& windowId, const QString& screenId)
{
    // Preserve the window's pre-autotile geometry before onWindowClosed clears it.
    // When the window is re-added on the target desktop, this geometry is restored
    // so that float-restore returns to the original position, not the tiled frame.
    if (m_preAutotileGeometries.contains(screenId)) {
        const auto& screenGeometries = m_preAutotileGeometries[screenId];
        const QString savedKey = AutotileStateHelpers::findSavedGeometryKey(screenGeometries, windowId);
        if (!savedKey.isEmpty()) {
            m_savedPreAutotileForDesktopMove[windowId] = screenGeometries.value(savedKey);
            qCDebug(lcEffect) << "Preserved pre-autotile geometry for desktop move:" << windowId
                              << m_savedPreAutotileForDesktopMove[windowId];
        }
    }
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
    if (m_lastFocusFollowsMouseWindowId == windowId) {
        m_lastFocusFollowsMouseWindowId.clear();
    }
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
        DBusHelpers::fireAndForget(m_effect, DBus::Interface::Autotile, QStringLiteral("windowClosed"), {windowId},
                                   QStringLiteral("windowClosed"));
        qCDebug(lcEffect) << "Notified autotile: windowClosed" << windowId << "on screen" << screenId;
    }
}

bool AutotileHandler::isEligibleForAutotileNotify(KWin::EffectWindow* w) const
{
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
    m_border.tiledWindows.remove(windowId);
    if (m_border.borderlessWindows.contains(windowId)) {
        KWin::EffectWindow* w = m_effect->findWindowById(windowId);
        if (w) {
            setWindowBorderless(w, windowId, false);
        }
    }
    m_effect->removeWindowBorder(windowId);
    unmaximizeMonocleWindow(windowId);
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

    bus.connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile, QStringLiteral("windowsTileRequested"),
                this, SLOT(slotWindowsTileRequested(PlasmaZones::TileRequestList)));

    bus.connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile, QStringLiteral("focusWindowRequested"),
                this, SLOT(slotFocusWindowRequested(QString)));

    bus.connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile, QStringLiteral("enabledChanged"), this,
                SLOT(slotEnabledChanged(bool)));

    bus.connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile,
                QStringLiteral("autotileScreensChanged"), this, SLOT(slotScreensChanged(QStringList, bool)));

    bus.connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile, QStringLiteral("windowFloatingChanged"),
                this, SLOT(slotWindowFloatingChanged(QString, bool, QString)));

    qCInfo(lcEffect) << "Connected to autotile D-Bus signals";
}

void AutotileHandler::loadSettings()
{
    // Query initial autotile screen set from daemon asynchronously.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        DBus::ServiceName, DBus::ObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << DBus::Interface::Autotile << QStringLiteral("autotileScreens");

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg);
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
