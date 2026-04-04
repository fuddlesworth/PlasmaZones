// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotilehandler.h"
#include "plasmazoneseffect.h"
#include "windowanimator.h"
#include "navigationhandler.h"
#include "dbus_constants.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QtMath>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

AutotileHandler::AutotileHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Static utility methods
// ═══════════════════════════════════════════════════════════════════════════════

QString AutotileHandler::findSavedGeometryKey(const QHash<QString, QRectF>& savedGeometries, const QString& windowId)
{
    // Exact windowId match only — no appId fallback.
    // A window opened during autotile has no pre-autotile geometry and must NOT
    // inherit another instance's geometry (e.g., 2 Ghostty windows: one snapped,
    // one opened during autotile — the unsnapped one would briefly jump to the
    // snapped one's zone position before being corrected by the daemon).
    auto it = savedGeometries.constFind(windowId);
    if (it != savedGeometries.constEnd()) {
        return it.key();
    }
    return QString();
}

bool AutotileHandler::hasSavedGeometryForWindow(const QHash<QString, QRectF>& savedGeometries, const QString& windowId)
{
    return !findSavedGeometryKey(savedGeometries, windowId).isEmpty();
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

std::optional<QRect> AutotileHandler::borderZoneGeometry(const QString& windowId) const
{
    auto it = m_border.zoneGeometries.constFind(windowId);
    if (it != m_border.zoneGeometries.constEnd()) {
        return it.value();
    }
    return std::nullopt;
}

QVector<QRect> AutotileHandler::allBorderZoneGeometries() const
{
    QVector<QRect> result;
    result.reserve(m_border.zoneGeometries.size());
    for (auto it = m_border.zoneGeometries.constBegin(); it != m_border.zoneGeometries.constEnd(); ++it) {
        result.append(it.value());
    }
    return result;
}

bool AutotileHandler::shouldApplyBorderInset(const QString& windowId) const
{
    return m_border.hideTitleBars && m_border.width > 0 && m_border.borderlessWindows.contains(windowId);
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
        m_effect->fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
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
    QJsonArray batchArr;
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

        QJsonObject obj;
        obj[QLatin1String("windowId")] = windowId;
        obj[QLatin1String("screenId")] = screenId;
        obj[QLatin1String("minWidth")] = minWidth;
        obj[QLatin1String("minHeight")] = minHeight;
        batchArr.append(obj);
        batchWindowIds.append(windowId);
    }

    if (batchArr.isEmpty()) {
        return;
    }

    QString json = QString::fromUtf8(QJsonDocument(batchArr).toJson(QJsonDocument::Compact));
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Autotile,
                                                      QStringLiteral("windowsOpenedBatch"));
    msg << json;

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
    qCInfo(lcEffect) << "Notified autotile: windowsOpenedBatch with" << batchArr.size() << "windows";
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
        const QString savedKey = findSavedGeometryKey(screenGeometries, windowId);
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

                    // If the drag already ended, apply immediately.
                    if (!safeW->isUserMove() && !safeW->isUserResize()) {
                        const QRectF frame = safeW->frameGeometry();
                        const QRect geo(qRound(frame.x()), qRound(frame.y()), restoreW, restoreH);
                        m_effect->applySnapGeometry(safeW, geo);
                        return;
                    }

                    // Still dragging — wait for drop.
                    m_pendingCrossScreenRestore[wid] =
                        connect(safeW.data(), &KWin::EffectWindow::windowFinishUserMovedResized, m_effect,
                                [this, safeW, wid, restoreW, restoreH](KWin::EffectWindow*) {
                                    m_pendingCrossScreenRestore.remove(wid);
                                    if (!safeW || safeW->isDeleted()) {
                                        return;
                                    }
                                    // Guard: window may have bounced back to autotile during drag.
                                    const QString dropScreen = m_effect->getWindowScreenId(safeW);
                                    if (m_autotileScreens.contains(dropScreen)) {
                                        return;
                                    }
                                    const QRectF frame = safeW->frameGeometry();
                                    const QRect geo(qRound(frame.x()), qRound(frame.y()), restoreW, restoreH);
                                    m_effect->applySnapGeometry(safeW, geo);
                                });
                });

        // Unsnap the window so it becomes floating on the new screen
        // instead of retaining a stale zone assignment from autotile.
        m_effect->fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("windowUnsnapped"), {windowId},
                                        QStringLiteral("autotile-to-snap unsnap"));

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
        const QString savedKey = findSavedGeometryKey(screenGeometries, windowId);
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

    // Remove from autotile tracking sets so re-opened windows get re-notified.
    m_notifiedWindows.remove(windowId);
    m_notifiedWindowScreens.remove(windowId);
    m_savedNotifiedForDesktopReturn.remove(windowId);
    m_minimizeFloatedWindows.remove(windowId);

    // Clean up borderless, monocle-maximize, deferred-centering, border zone, focus-follows-mouse,
    // and pre-autotile tracking
    m_border.borderlessWindows.remove(windowId);
    m_border.tiledWindows.remove(windowId);
    m_border.zoneGeometries.remove(windowId);
    m_monocleMaximizedWindows.remove(windowId);
    m_autotileTargetZones.remove(windowId);
    m_centeredWaylandZones.remove(windowId);
    if (m_lastFocusFollowsMouseWindowId == windowId) {
        m_lastFocusFollowsMouseWindowId.clear();
    }
    if (m_preAutotileGeometries.contains(screenId)) {
        m_preAutotileGeometries[screenId].remove(windowId);
    }
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
        m_effect->fireAndForgetDBusCall(DBus::Interface::Autotile, QStringLiteral("windowClosed"), {windowId},
                                        QStringLiteral("windowClosed"));
        qCDebug(lcEffect) << "Notified autotile: windowClosed" << windowId << "on screen" << screenId;
    }
}

bool AutotileHandler::isEligibleForAutotileNotify(KWin::EffectWindow* w) const
{
    if (!w || !m_effect->shouldHandleWindow(w)) {
        return false;
    }
    if (!m_effect->isTileableWindow(w)) {
        return false;
    }
    if (w->isMinimized()) {
        return false;
    }
    if (!w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
        return false;
    }
    // Reject windows smaller than the user-configured minimum size.
    // Prevents small utility windows (emoji picker, color picker, etc.)
    // from entering the tiling tree and disrupting the layout.
    const QRectF frame = w->frameGeometry();
    if ((m_effect->m_cachedMinWindowWidth > 0 && frame.width() < m_effect->m_cachedMinWindowWidth)
        || (m_effect->m_cachedMinWindowHeight > 0 && frame.height() < m_effect->m_cachedMinWindowHeight)) {
        return false;
    }
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
                this, SLOT(slotWindowsTileRequested(QString)));

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
