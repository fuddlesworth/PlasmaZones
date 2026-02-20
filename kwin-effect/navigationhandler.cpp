// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "navigationhandler.h"
#include "plasmazoneseffect.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace PlasmaZones {

// Navigation directive prefixes
static const QString NavigateDirectivePrefix = QStringLiteral("navigate:");
static const QString PushDirective = QStringLiteral("push");
static const QString SnapDirectivePrefix = QStringLiteral("snap:");
static const QString SwapDirectivePrefix = QStringLiteral("swap:");
static const QString CycleDirectivePrefix = QStringLiteral("cycle:");

NavigationHandler::NavigationHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void NavigationHandler::handleMoveWindowToZone(const QString& targetZoneId, const QString& zoneGeometry)
{
    qCInfo(lcEffect) << "Move window to zone requested -" << targetZoneId;

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("move"));
    if (!activeWindow) {
        return;
    }

    QString windowId = m_effect->getWindowId(activeWindow);
    QString stableId = m_effect->extractStableId(windowId);
    QString screenName = m_effect->getWindowScreenName(activeWindow);

    // User-initiated snap commands override floating state.
    // windowSnapped() on the daemon will clear floating via clearFloatingStateForSnap().

    // Check if this is a navigation directive - use daemon-driven flow
    if (targetZoneId.startsWith(NavigateDirectivePrefix)) {
        QString direction = targetZoneId.mid(NavigateDirectivePrefix.length());
        qCDebug(lcEffect) << "Navigation direction (daemon-driven):" << direction;

        auto* iface = m_effect->windowTrackingInterface();
        if (!iface || !iface->isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("dbus_error"),
                                             QString(), QString(), screenName);
            return;
        }

        QPointer<KWin::EffectWindow> safeWindow = activeWindow;
        QString capturedWindowId = windowId;
        QString capturedScreenName = screenName;
        QRectF capturedPreSnapGeom = activeWindow->frameGeometry();

        QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getMoveTargetForWindow"),
                                                        windowId, direction, screenName);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeWindow, capturedWindowId, capturedScreenName, capturedPreSnapGeom](QDBusPendingCallWatcher* w) {
            w->deleteLater();

            if (!safeWindow) return;

            QDBusPendingReply<QString> reply = *w;
            if (!reply.isValid()) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("dbus_error"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
            if (!doc.isObject()) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("parse_error"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            QJsonObject obj = doc.object();
            if (!obj.value(QLatin1String("success")).toBool(false)) {
                return; // Daemon already emitted navigationFeedback
            }

            QString zoneId = obj.value(QLatin1String("zoneId")).toString();
            QString geometryJson = obj.value(QLatin1String("geometryJson")).toString();
            QString sourceZoneId = obj.value(QLatin1String("sourceZoneId")).toString();

            QRect geometry = m_effect->parseZoneGeometry(geometryJson);
            if (!geometry.isValid()) return;

            if (sourceZoneId.isEmpty()) {
                m_effect->ensurePreSnapGeometryStored(safeWindow, capturedWindowId, capturedPreSnapGeom);
            }

            m_effect->applySnapGeometry(safeWindow, geometry);
            auto* snapIface = m_effect->windowTrackingInterface();
            if (snapIface && snapIface->isValid()) {
                snapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, zoneId, capturedScreenName);
                snapIface->asyncCall(QStringLiteral("recordSnapIntent"), capturedWindowId, true);
            }
        });

    } else if (targetZoneId == PushDirective) {
        // Push to empty zone - daemon-driven
        QString pushScreenName = zoneGeometry;
        if (pushScreenName.isEmpty()) {
            pushScreenName = screenName;
        }

        auto* iface = m_effect->windowTrackingInterface();
        if (!iface || !iface->isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("dbus_error"),
                                             QString(), QString(), pushScreenName);
            return;
        }

        QPointer<KWin::EffectWindow> safeWindow = activeWindow;
        QString capturedWindowId = windowId;
        QString capturedScreenName = pushScreenName;
        QRectF capturedPreSnapGeom = activeWindow->frameGeometry();

        QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getPushTargetForWindow"),
                                                        windowId, pushScreenName);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeWindow, capturedWindowId, capturedScreenName, capturedPreSnapGeom](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            if (!safeWindow) return;

            QDBusPendingReply<QString> reply = *w;
            if (!reply.isValid()) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("dbus_error"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
            if (!doc.isObject()) return;

            QJsonObject obj = doc.object();
            if (!obj.value(QLatin1String("success")).toBool(false)) return;

            QString zoneId = obj.value(QLatin1String("zoneId")).toString();
            QString geometryJson = obj.value(QLatin1String("geometryJson")).toString();
            QString sourceZoneId = obj.value(QLatin1String("sourceZoneId")).toString();

            QRect geometry = m_effect->parseZoneGeometry(geometryJson);
            if (!geometry.isValid()) return;

            if (sourceZoneId.isEmpty()) {
                m_effect->ensurePreSnapGeometryStored(safeWindow, capturedWindowId, capturedPreSnapGeom);
            }

            m_effect->applySnapGeometry(safeWindow, geometry);
            auto* snapIface = m_effect->windowTrackingInterface();
            if (snapIface && snapIface->isValid()) {
                snapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, zoneId, capturedScreenName);
                snapIface->asyncCall(QStringLiteral("recordSnapIntent"), capturedWindowId, true);
            }
        });

    } else if (targetZoneId.startsWith(SnapDirectivePrefix)) {
        // Snap to zone by number - daemon-driven
        bool ok = false;
        int zoneNumber = targetZoneId.mid(SnapDirectivePrefix.length()).toInt(&ok);
        if (!ok || zoneNumber < 1 || zoneNumber > 9) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"),
                                             QString(), QString(), screenName);
            return;
        }

        QString snapScreenName = zoneGeometry;
        if (snapScreenName.isEmpty()) {
            snapScreenName = screenName;
        }

        auto* iface = m_effect->windowTrackingInterface();
        if (!iface || !iface->isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("snap"), QStringLiteral("dbus_error"),
                                             QString(), QString(), snapScreenName);
            return;
        }

        QPointer<KWin::EffectWindow> safeWindow = activeWindow;
        QString capturedWindowId = windowId;
        QString capturedScreenName = snapScreenName;
        QRectF capturedPreSnapGeom = activeWindow->frameGeometry();

        QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getSnapToZoneByNumberTarget"),
                                                        windowId, zoneNumber, snapScreenName);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeWindow, capturedWindowId, capturedScreenName, capturedPreSnapGeom](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            if (!safeWindow) return;

            QDBusPendingReply<QString> reply = *w;
            if (!reply.isValid()) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("snap"), QStringLiteral("dbus_error"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
            if (!doc.isObject()) return;

            QJsonObject obj = doc.object();
            if (!obj.value(QLatin1String("success")).toBool(false)) return;

            QString zoneId = obj.value(QLatin1String("zoneId")).toString();
            QString geometryJson = obj.value(QLatin1String("geometryJson")).toString();
            QString sourceZoneId = obj.value(QLatin1String("sourceZoneId")).toString();

            QRect geometry = m_effect->parseZoneGeometry(geometryJson);
            if (!geometry.isValid()) return;

            if (sourceZoneId.isEmpty()) {
                m_effect->ensurePreSnapGeometryStored(safeWindow, capturedWindowId, capturedPreSnapGeom);
            }

            m_effect->applySnapGeometry(safeWindow, geometry);
            auto* snapIface = m_effect->windowTrackingInterface();
            if (snapIface && snapIface->isValid()) {
                snapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, zoneId, capturedScreenName);
                snapIface->asyncCall(QStringLiteral("recordSnapIntent"), capturedWindowId, true);
            }
        });

    } else if (!targetZoneId.isEmpty()) {
        // Direct zone ID (legacy - e.g. from moveSpecificWindowToZoneRequested for snap assist)
        // The daemon provides geometry but it may be for the primary screen, not the window's screen.
        // Use ASYNC D-Bus call to get screen-specific geometry.

        auto* iface = m_effect->windowTrackingInterface();
        if (!iface || !iface->isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("dbus_error"),
                                             QString(), QString(), screenName);
            return;
        }

        QPointer<KWin::EffectWindow> safeWindow = activeWindow;
        QString capturedWindowId = windowId;
        QString capturedTargetZoneId = targetZoneId;
        QString capturedScreenName = screenName;
        QString capturedFallbackGeometry = zoneGeometry;
        QRectF preSnapGeom = activeWindow->frameGeometry();
        bool hasValidPreSnapGeom = preSnapGeom.width() > 0 && preSnapGeom.height() > 0;

        QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getZoneGeometryForScreen"), targetZoneId, screenName);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeWindow, capturedWindowId, capturedTargetZoneId, capturedScreenName, capturedFallbackGeometry, preSnapGeom, hasValidPreSnapGeom](QDBusPendingCallWatcher* w) {
            w->deleteLater();

            if (!safeWindow) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("window_destroyed"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            QDBusPendingReply<QString> reply = *w;
            QString geometryJson = reply.isValid() && !reply.value().isEmpty() ? reply.value() : capturedFallbackGeometry;

            QRect geometry = m_effect->parseZoneGeometry(geometryJson);
            if (!geometry.isValid()) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            if (hasValidPreSnapGeom) {
                auto* innerIface = m_effect->windowTrackingInterface();
                if (innerIface && innerIface->isValid()) {
                    QDBusPendingCall hasGeomCall = innerIface->asyncCall(QStringLiteral("hasPreSnapGeometry"), capturedWindowId);
                    auto* hasGeomWatcher = new QDBusPendingCallWatcher(hasGeomCall, this);
                    connect(hasGeomWatcher, &QDBusPendingCallWatcher::finished, this,
                            [this, capturedWindowId, preSnapGeom](QDBusPendingCallWatcher* hgw) {
                        hgw->deleteLater();
                        QDBusPendingReply<bool> hasGeomReply = *hgw;
                        if (!hasGeomReply.isValid() || !hasGeomReply.value()) {
                            auto* storeIface = m_effect->windowTrackingInterface();
                            if (storeIface && storeIface->isValid()) {
                                storeIface->asyncCall(QStringLiteral("storePreSnapGeometry"), capturedWindowId,
                                                 static_cast<int>(preSnapGeom.x()), static_cast<int>(preSnapGeom.y()),
                                                 static_cast<int>(preSnapGeom.width()), static_cast<int>(preSnapGeom.height()));
                            }
                        }
                    });
                }
            }

            m_effect->applySnapGeometry(safeWindow, geometry);
            auto* snapIface = m_effect->windowTrackingInterface();
            if (snapIface && snapIface->isValid()) {
                snapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZoneId, capturedScreenName);
            }

            m_effect->emitNavigationFeedback(true, QStringLiteral("push"), QString(), QString(), capturedTargetZoneId, capturedScreenName);
        });
    }
}

void NavigationHandler::handleFocusWindowInZone(const QString& targetZoneId, const QString& windowId)
{
    Q_UNUSED(windowId)
    qCInfo(lcEffect) << "Focus window in zone requested -" << targetZoneId;

    if (targetZoneId.isEmpty()) {
        return;
    }

    QString screenName; // For OSD placement

    // Default screen from active window (used when targetZoneId is a direct zone ID)
    KWin::EffectWindow* activeWindow = m_effect->getActiveWindow();
    if (activeWindow) {
        screenName = m_effect->getWindowScreenName(activeWindow);
    }

    // Helper: async getWindowsInZone -> find and activate target window
    auto doFocusInZone = [this](const QString& sourceZoneId, const QString& zoneId, const QString& screen) {
        auto* iface = m_effect->windowTrackingInterface();
        if (!iface || !iface->isValid()) {
            return;
        }

        QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getWindowsInZone"), zoneId);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, sourceZoneId, zoneId, screen](QDBusPendingCallWatcher* w) {
            w->deleteLater();

            QDBusPendingReply<QStringList> reply = *w;
            if (!reply.isValid() || reply.value().isEmpty()) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window_in_zone"),
                                                 QString(), QString(), screen);
                return;
            }

            QStringList windowsInZone = reply.value();
            QString targetWindowId = windowsInZone.first();
            const auto windows = KWin::effects->stackingOrder();
            for (KWin::EffectWindow* win : windows) {
                if (win && m_effect->getWindowId(win) == targetWindowId) {
                    KWin::effects->activateWindow(win);
                    m_effect->emitNavigationFeedback(true, QStringLiteral("focus"), QString(),
                                                     sourceZoneId, zoneId, screen);
                    return;
                }
            }

            m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("window_not_found"),
                                             QString(), QString(), screen);
        });
    };

    if (targetZoneId.startsWith(NavigateDirectivePrefix)) {
        QString direction = targetZoneId.mid(NavigateDirectivePrefix.length());
        if (!activeWindow) return;

        QString activeWindowId = m_effect->getWindowId(activeWindow);
        QString capturedScreenName = screenName;

        auto* iface = m_effect->windowTrackingInterface();
        if (!iface || !iface->isValid()) return;

        QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getFocusTargetForWindow"),
                                                        activeWindowId, direction, capturedScreenName);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, capturedScreenName](QDBusPendingCallWatcher* w) {
            w->deleteLater();

            QDBusPendingReply<QString> reply = *w;
            if (!reply.isValid()) return;

            QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
            if (!doc.isObject()) return;

            QJsonObject obj = doc.object();
            if (!obj.value(QLatin1String("success")).toBool(false)) return;

            QString windowIdToActivate = obj.value(QLatin1String("windowIdToActivate")).toString();
            if (windowIdToActivate.isEmpty()) return;

            const auto windows = KWin::effects->stackingOrder();
            for (KWin::EffectWindow* win : windows) {
                if (win && m_effect->getWindowId(win) == windowIdToActivate) {
                    KWin::effects->activateWindow(win);
                    return;
                }
            }
        });
    } else {
        // Direct zone ID - proceed directly to async getWindowsInZone
        doFocusInZone(QString(), targetZoneId, screenName);
    }
}

void NavigationHandler::handleRestoreWindow()
{
    qCInfo(lcEffect) << "Restore window requested (daemon-driven)";

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("restore"));
    if (!activeWindow) return;

    QString windowId = m_effect->getWindowId(activeWindow);
    QString screenName = m_effect->getWindowScreenName(activeWindow);
    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("restore"), QStringLiteral("dbus_error"),
                                         QString(), QString(), screenName);
        return;
    }

    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    QString capturedWindowId = windowId;
    QString capturedScreenName = screenName;

    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getRestoreForWindow"), windowId, screenName);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, capturedWindowId, capturedScreenName](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        if (!safeWindow) return;

        QDBusPendingReply<QString> reply = *w;
        if (!reply.isValid()) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
        if (!doc.isObject()) return;

        QJsonObject obj = doc.object();
        if (!obj.value(QLatin1String("success")).toBool(false)) return;

        int x = obj.value(QLatin1String("x")).toInt();
        int y = obj.value(QLatin1String("y")).toInt();
        int width = obj.value(QLatin1String("width")).toInt();
        int height = obj.value(QLatin1String("height")).toInt();
        if (width <= 0 || height <= 0) return;

        QRect geometry(x, y, width, height);
        m_effect->applySnapGeometry(safeWindow, geometry);

        auto* innerIface = m_effect->windowTrackingInterface();
        if (innerIface && innerIface->isValid()) {
            innerIface->asyncCall(QStringLiteral("windowUnsnapped"), capturedWindowId);
            innerIface->asyncCall(QStringLiteral("clearPreSnapGeometry"), capturedWindowId);
        }
    });
}

void NavigationHandler::handleToggleWindowFloat(bool shouldFloat)
{
    Q_UNUSED(shouldFloat)
    qCInfo(lcEffect) << "Toggle float requested";

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("float"));
    if (!activeWindow) {
        return;
    }

    QString windowId = m_effect->getWindowId(activeWindow);
    QString screenName = m_effect->getWindowScreenName(activeWindow);

    auto* iface = m_effect->windowTrackingInterface();

    // Query daemon's floating state async to ensure local cache is in sync.
    // This fixes race conditions where windowFloatingChanged signal hasn't arrived yet
    // (e.g., after drag-snapping a floating window).
    // Use full windowId so the daemon can distinguish multiple instances of the same app.
    if (iface && iface->isValid()) {
        QPointer<KWin::EffectWindow> safeWindow = activeWindow;
        QString capturedWindowId = windowId;
        QString capturedScreenName = screenName;

        QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("queryWindowFloating"), windowId);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeWindow, capturedWindowId, capturedScreenName](QDBusPendingCallWatcher* w) {
            w->deleteLater();

            if (!safeWindow) {
                qCDebug(lcEffect) << "Window destroyed during float toggle query";
                return;
            }

            QDBusPendingReply<bool> reply = *w;
            bool daemonFloating = false;
            if (reply.isValid()) {
                daemonFloating = reply.value();
                // Sync local cache with daemon state (use full windowId for per-instance tracking)
                if (daemonFloating != isWindowFloating(capturedWindowId)) {
                    qCDebug(lcEffect) << "Syncing floating state from daemon: windowId=" << capturedWindowId
                                      << "local=" << isWindowFloating(capturedWindowId)
                                      << "daemon=" << daemonFloating;
                    setWindowFloating(capturedWindowId, daemonFloating);
                }
            }

            // Now perform the toggle with accurate state
            bool newFloatState = !daemonFloating;
            executeFloatToggle(safeWindow, capturedWindowId, capturedScreenName, newFloatState);
        });
        return; // Actual toggle happens in async callback
    }

    // Fallback if D-Bus not available - use local cache
    bool isCurrentlyFloating = isWindowFloating(windowId);
    bool newFloatState = !isCurrentlyFloating;

    executeFloatToggle(activeWindow, windowId, screenName, newFloatState);
}

void NavigationHandler::executeFloatToggle(KWin::EffectWindow* activeWindow, const QString& windowId,
                                            const QString& screenName, bool newFloatState)
{
    auto* iface = m_effect->windowTrackingInterface();

    if (newFloatState) {
        // Floating ON - restore pre-snap geometry (like drag-unsnap does) and mark as floating
        m_floatingWindows.insert(windowId);

        qCInfo(lcEffect) << "Floating window:" << windowId;

        if (iface && iface->isValid()) {
            // Use QPointer for safe async handling
            QPointer<KWin::EffectWindow> safeWindow = activeWindow;
            QString capturedWindowId = windowId;
            QString capturedScreenName = screenName;

            // Get pre-snap geometry and restore it (matching drag-unsnap behavior)
            QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getValidatedPreSnapGeometry"), windowId);
            auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

            connect(watcher, &QDBusPendingCallWatcher::finished, this,
                    [this, safeWindow, capturedWindowId, capturedScreenName](QDBusPendingCallWatcher* w) {
                w->deleteLater();

                auto* iface = m_effect->windowTrackingInterface();

                // Process the geometry reply
                QDBusPendingReply<bool, int, int, int, int> reply = *w;
                if (reply.isValid() && reply.count() >= 5) {
                    bool found = reply.argumentAt<0>();
                    int x = reply.argumentAt<1>();
                    int y = reply.argumentAt<2>();
                    int width = reply.argumentAt<3>();
                    int height = reply.argumentAt<4>();

                    if (found && width > 0 && height > 0 && safeWindow) {
                        QRect geometry(x, y, width, height);
                        qCDebug(lcEffect) << "Restoring pre-snap geometry on float:" << geometry;
                        m_effect->applySnapGeometry(safeWindow, geometry);
                    }
                }

                // Unsnap from zone and mark as floating regardless of geometry restoration
                if (iface && iface->isValid()) {
                    iface->asyncCall(QStringLiteral("windowUnsnappedForFloat"), capturedWindowId);
                    iface->asyncCall(QStringLiteral("setWindowFloating"), capturedWindowId, true);
                    iface->asyncCall(QStringLiteral("clearPreSnapGeometry"), capturedWindowId);
                }

                m_effect->emitNavigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"),
                                                 QString(), QString(), capturedScreenName);
            });

            return; // Feedback emitted in async callback
        }

        m_effect->emitNavigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"),
                                         QString(), QString(), screenName);
    } else {
        // Floating OFF - restore to previous zone if available
        m_floatingWindows.remove(windowId);
        // Also remove stableId entry (session-restored entries)
        QString stableId = m_effect->extractStableId(windowId);
        if (stableId != windowId) {
            m_floatingWindows.remove(stableId);
        }

        if (iface && iface->isValid()) {
            iface->asyncCall(QStringLiteral("setWindowFloating"), windowId, false);

            // Use QPointer for safe async handling
            QPointer<KWin::EffectWindow> safeWindow = activeWindow;
            QString capturedWindowId = windowId;
            QString capturedScreenName = screenName;

            // Use single ASYNC D-Bus call to get pre-float zones + combined geometry
            QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("calculateUnfloatRestore"), windowId, screenName);
            auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

            connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, safeWindow, capturedWindowId, capturedScreenName](QDBusPendingCallWatcher* w) {
                w->deleteLater();

                QDBusPendingReply<QString> reply = *w;
                if (!reply.isValid()) {
                    qCDebug(lcEffect) << "calculateUnfloatRestore reply invalid:" << reply.error().message();
                    return;
                }

                QString restoreJson = reply.value();
                qCDebug(lcEffect) << "calculateUnfloatRestore result:" << restoreJson;

                QJsonDocument doc = QJsonDocument::fromJson(restoreJson.toUtf8());
                if (!doc.isObject()) {
                    qCDebug(lcEffect) << "calculateUnfloatRestore: invalid JSON";
                    return;
                }

                QJsonObject obj = doc.object();
                bool found = obj.value(QStringLiteral("found")).toBool(false);
                if (!found) {
                    qCDebug(lcEffect) << "No pre-float zone found for window";
                    return;
                }

                if (!safeWindow) {
                    qCDebug(lcEffect) << "Window was destroyed during async call";
                    return;
                }

                // Extract zone IDs
                QStringList zoneIds;
                const QJsonArray zoneArray = obj.value(QStringLiteral("zoneIds")).toArray();
                for (const QJsonValue& v : zoneArray) {
                    zoneIds.append(v.toString());
                }

                // Extract combined geometry
                QRect geometry(obj.value(QStringLiteral("x")).toInt(),
                               obj.value(QStringLiteral("y")).toInt(),
                               obj.value(QStringLiteral("width")).toInt(),
                               obj.value(QStringLiteral("height")).toInt());

                // Extract the screen where the zone was originally snapped
                QString restoreScreen = obj.value(QStringLiteral("screenName")).toString();

                if (!geometry.isValid() || zoneIds.isEmpty()) {
                    qCDebug(lcEffect) << "Invalid geometry or empty zones for unfloat";
                    return;
                }

                // BUG FIX: Store the current floating geometry as pre-snap BEFORE snapping to zone.
                // This allows the next float toggle to restore the window to its floating position.
                // Without this, float→unfloat→float would fail because there's no geometry to restore.
                auto* iface = m_effect->windowTrackingInterface();
                if (iface && iface->isValid()) {
                    QRectF floatingGeom = safeWindow->frameGeometry();
                    qCDebug(lcEffect) << "Storing floating geometry as pre-snap:" << floatingGeom;
                    iface->asyncCall(QStringLiteral("storePreSnapGeometry"), capturedWindowId,
                                     static_cast<int>(floatingGeom.x()), static_cast<int>(floatingGeom.y()),
                                     static_cast<int>(floatingGeom.width()), static_cast<int>(floatingGeom.height()));
                }

                qCInfo(lcEffect) << "Applying unfloat geometry:" << geometry << "to zones:" << zoneIds
                                  << "on screen:" << restoreScreen;
                m_effect->applySnapGeometry(safeWindow, geometry);

                if (iface && iface->isValid()) {
                    if (zoneIds.size() > 1) {
                        iface->asyncCall(QStringLiteral("windowSnappedMultiZone"), capturedWindowId, zoneIds, restoreScreen);
                    } else {
                        iface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, zoneIds.first(), restoreScreen);
                    }
                    iface->asyncCall(QStringLiteral("clearPreFloatZone"), capturedWindowId);
                }
            });
        }

        m_effect->emitNavigationFeedback(true, QStringLiteral("float"), QStringLiteral("unfloated"),
                                         QString(), QString(), screenName);
    }
}

void NavigationHandler::handleSwapWindows(const QString& targetZoneId, const QString& targetWindowId,
                                          const QString& zoneGeometry)
{
    Q_UNUSED(targetWindowId)
    Q_UNUSED(zoneGeometry)
    qCInfo(lcEffect) << "Swap windows requested (daemon-driven) -" << targetZoneId;

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("swap"));
    if (!activeWindow) return;

    QString windowId = m_effect->getWindowId(activeWindow);
    QString screenName = m_effect->getWindowScreenName(activeWindow);

    if (!targetZoneId.startsWith(SwapDirectivePrefix)) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_directive"),
                                         QString(), QString(), screenName);
        return;
    }

    QString direction = targetZoneId.mid(SwapDirectivePrefix.length());

    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("dbus_error"),
                                         QString(), QString(), screenName);
        return;
    }

    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    QString capturedWindowId = windowId;
    QString capturedScreenName = screenName;

    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getSwapTargetForWindow"),
                                                    windowId, direction, screenName);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, capturedWindowId, capturedScreenName](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        if (!safeWindow) return;

        QDBusPendingReply<QString> reply = *w;
        if (!reply.isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("dbus_error"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
        if (!doc.isObject()) return;

        QJsonObject obj = doc.object();
        if (!obj.value(QLatin1String("success")).toBool(false)) return;

        int x1 = obj.value(QLatin1String("x1")).toInt();
        int y1 = obj.value(QLatin1String("y1")).toInt();
        int w1 = obj.value(QLatin1String("w1")).toInt();
        int h1 = obj.value(QLatin1String("h1")).toInt();
        QString zoneId1 = obj.value(QLatin1String("zoneId1")).toString();
        QString windowId2 = obj.value(QLatin1String("windowId2")).toString();
        int x2 = obj.value(QLatin1String("x2")).toInt();
        int y2 = obj.value(QLatin1String("y2")).toInt();
        int w2 = obj.value(QLatin1String("w2")).toInt();
        int h2 = obj.value(QLatin1String("h2")).toInt();
        QString zoneId2 = obj.value(QLatin1String("zoneId2")).toString();
        QString resultScreenName = obj.value(QLatin1String("screenName")).toString();

        QRect geom1(x1, y1, w1, h1);
        if (!geom1.isValid()) return;

        auto* swapIface = m_effect->windowTrackingInterface();
        if (!swapIface || !swapIface->isValid()) return;

        if (windowId2.isEmpty()) {
            m_effect->applySnapGeometry(safeWindow, geom1);
            swapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, zoneId1, resultScreenName);
        } else {
            QRect geom2(x2, y2, w2, h2);
            if (!geom2.isValid()) return;

            KWin::EffectWindow* targetWindow = nullptr;
            const auto windows = KWin::effects->stackingOrder();
            for (KWin::EffectWindow* win : windows) {
                if (win && m_effect->getWindowId(win) == windowId2) {
                    targetWindow = win;
                    break;
                }
            }

            if (!targetWindow || !m_effect->shouldHandleWindow(targetWindow)) {
                m_effect->applySnapGeometry(safeWindow, geom1);
                swapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, zoneId1, resultScreenName);
                return;
            }

            m_effect->ensurePreSnapGeometryStored(safeWindow, capturedWindowId);
            m_effect->ensurePreSnapGeometryStored(targetWindow, windowId2);

            m_effect->applySnapGeometry(safeWindow, geom1);
            swapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, zoneId1, resultScreenName);

            m_effect->applySnapGeometry(targetWindow, geom2);
            swapIface->asyncCall(QStringLiteral("windowSnapped"), windowId2, zoneId2, resultScreenName);
        }
    });
}

NavigationHandler::BatchSnapResult NavigationHandler::applyBatchSnapFromJson(const QString& jsonData,
                                                                             bool filterCurrentDesktop,
                                                                             bool resolveFullWindowId)
{
    BatchSnapResult result;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        result.status = BatchSnapResult::ParseError;
        return result;
    }

    QJsonArray entries = doc.array();
    if (entries.isEmpty()) {
        result.status = BatchSnapResult::EmptyData;
        return result;
    }

    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        result.status = BatchSnapResult::DbusError;
        return result;
    }

    QHash<QString, KWin::EffectWindow*> windowMap = m_effect->buildWindowMap();

    for (const QJsonValue& value : entries) {
        if (!value.isObject()) {
            continue;
        }

        QJsonObject moveObj = value.toObject();
        QString windowId = moveObj[QLatin1String("windowId")].toString();
        QString targetZoneId = moveObj[QLatin1String("targetZoneId")].toString();
        int x = moveObj[QLatin1String("x")].toInt();
        int y = moveObj[QLatin1String("y")].toInt();
        int width = moveObj[QLatin1String("width")].toInt();
        int height = moveObj[QLatin1String("height")].toInt();

        if (windowId.isEmpty() || targetZoneId.isEmpty()) {
            continue;
        }

        QString stableId = m_effect->extractStableId(windowId);
        KWin::EffectWindow* window = windowMap.value(stableId);

        if (!window) {
            continue;
        }
        // User-initiated snap commands override floating state.
        // windowSnapped() on the daemon will clear floating via clearFloatingStateForSnap().

        if (filterCurrentDesktop && (!window->isOnCurrentDesktop() || !window->isOnCurrentActivity())) {
            continue;
        }

        // Resnap JSON may contain stableIds from pending entries; resolve full windowId from KWin
        QString snapWindowId = resolveFullWindowId ? m_effect->getWindowId(window) : windowId;

        m_effect->ensurePreSnapGeometryStored(window, snapWindowId);
        m_effect->applySnapGeometry(window, QRect(x, y, width, height));
        QString windowScreen = m_effect->getWindowScreenName(window);
        iface->asyncCall(QStringLiteral("windowSnapped"), snapWindowId, targetZoneId, windowScreen);
        ++result.successCount;

        if (result.successCount == 1) {
            result.firstSourceZoneId = moveObj[QLatin1String("sourceZoneId")].toString();
            result.firstTargetZoneId = targetZoneId;
        }
    }

    return result;
}

void NavigationHandler::handleRotateWindows(bool clockwise, const QString& rotationData)
{
    qCInfo(lcEffect) << "Rotate windows requested, clockwise:" << clockwise;

    KWin::EffectWindow* activeWindow = m_effect->getActiveWindow();
    QString screenName = activeWindow ? m_effect->getWindowScreenName(activeWindow) : QString();

    BatchSnapResult result = applyBatchSnapFromJson(rotationData);

    if (result.status == BatchSnapResult::ParseError) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("parse_error"),
                                         QString(), QString(), screenName);
    } else if (result.status == BatchSnapResult::EmptyData) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_windows"),
                                         QString(), QString(), screenName);
    } else if (result.status == BatchSnapResult::DbusError) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("dbus_error"),
                                         QString(), QString(), screenName);
    } else if (result.successCount > 0) {
        QString direction = clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise");
        QString reason = QStringLiteral("%1:%2").arg(direction).arg(result.successCount);
        m_effect->emitNavigationFeedback(true, QStringLiteral("rotate"), reason,
                                         result.firstSourceZoneId, result.firstTargetZoneId, screenName);
    } else {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_rotations"),
                                         QString(), QString(), screenName);
    }
}

void NavigationHandler::handleResnapToNewLayout(const QString& resnapData)
{
    qCInfo(lcEffect) << "Resnap to new layout requested";

    KWin::EffectWindow* activeWindow = m_effect->getActiveWindow();
    QString screenName = activeWindow ? m_effect->getWindowScreenName(activeWindow) : QString();

    BatchSnapResult result = applyBatchSnapFromJson(resnapData, /*filterCurrentDesktop=*/true,
                                                    /*resolveFullWindowId=*/true);

    if (result.status == BatchSnapResult::ParseError) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("parse_error"),
                                         QString(), QString(), screenName);
    } else if (result.status == BatchSnapResult::EmptyData) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_windows"),
                                         QString(), QString(), screenName);
    } else if (result.status == BatchSnapResult::DbusError) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("dbus_error"),
                                         QString(), QString(), screenName);
    } else if (result.successCount > 0) {
        QString reason = QStringLiteral("resnap:%1").arg(result.successCount);
        m_effect->emitNavigationFeedback(true, QStringLiteral("resnap"), reason,
                                         QString(), result.firstTargetZoneId, screenName);
    } else {
        m_effect->emitNavigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_resnaps"),
                                         QString(), QString(), screenName);
    }
}

void NavigationHandler::handleSnapAllWindows(const QString& snapData, const QString& screenName)
{
    qCDebug(lcEffect) << "Snap all windows handler called for screen:" << screenName;

    BatchSnapResult result = applyBatchSnapFromJson(snapData);

    if (result.status == BatchSnapResult::ParseError) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("parse_error"),
                                         QString(), QString(), screenName);
    } else if (result.status == BatchSnapResult::EmptyData) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_windows"),
                                         QString(), QString(), screenName);
    } else if (result.status == BatchSnapResult::DbusError) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("dbus_error"),
                                         QString(), QString(), screenName);
    } else if (result.successCount > 0) {
        QString reason = QStringLiteral("snap_all:%1").arg(result.successCount);
        m_effect->emitNavigationFeedback(true, QStringLiteral("snap_all"), reason,
                                         QString(), result.firstTargetZoneId, screenName);
    } else {
        m_effect->emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_snaps"),
                                         QString(), QString(), screenName);
    }
}

void NavigationHandler::handleCycleWindowsInZone(const QString& directive, const QString& unused)
{
    Q_UNUSED(unused)
    qCInfo(lcEffect) << "Cycle windows in zone requested (daemon-driven) -" << directive;

    if (!directive.startsWith(CycleDirectivePrefix)) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("invalid_directive"));
        return;
    }

    QString direction = directive.mid(CycleDirectivePrefix.length());
    bool forward = (direction == QStringLiteral("forward"));
    if (direction != QStringLiteral("forward") && direction != QStringLiteral("backward")) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("invalid_direction"));
        return;
    }

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("cycle"));
    if (!activeWindow) return;

    QString windowId = m_effect->getWindowId(activeWindow);
    QString screenName = m_effect->getWindowScreenName(activeWindow);

    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("dbus_error"),
                                         QString(), QString(), screenName);
        return;
    }

    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getCycleTargetForWindow"),
                                                    windowId, forward, screenName);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QString> reply = *w;
        if (!reply.isValid()) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
        if (!doc.isObject()) return;

        QJsonObject obj = doc.object();
        if (!obj.value(QLatin1String("success")).toBool(false)) return;

        QString windowIdToActivate = obj.value(QLatin1String("windowIdToActivate")).toString();
        if (windowIdToActivate.isEmpty()) return;

        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* win : windows) {
            if (win && m_effect->getWindowId(win) == windowIdToActivate) {
                KWin::effects->activateWindow(win);
                return;
            }
        }
    });
}

bool NavigationHandler::isWindowFloating(const QString& windowId) const
{
    // Try full window ID first (runtime - distinguishes multiple instances)
    if (m_floatingWindows.contains(windowId)) {
        return true;
    }
    // Fall back to stable ID (session restore - pointer addresses change across restarts)
    QString stableId = m_effect->extractStableId(windowId);
    return (stableId != windowId && m_floatingWindows.contains(stableId));
}

void NavigationHandler::setWindowFloating(const QString& windowId, bool floating)
{
    if (floating) {
        m_floatingWindows.insert(windowId);
    } else {
        m_floatingWindows.remove(windowId);
        // Also remove stable ID entry (session-restored entries)
        QString stableId = m_effect->extractStableId(windowId);
        if (stableId != windowId) {
            m_floatingWindows.remove(stableId);
        }
    }
}

void NavigationHandler::syncFloatingWindowsFromDaemon()
{
    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        return;
    }

    // Use ASYNC D-Bus call to avoid blocking the compositor thread during startup
    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getFloatingWindows"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QStringList> reply = *w;
        if (!reply.isValid()) {
            qCDebug(lcEffect) << "Failed to get floating windows from daemon";
            return;
        }

        QStringList floatingIds = reply.value();
        m_floatingWindows.clear();

        for (const QString& id : floatingIds) {
            // Store as-is: stableIds from session restore, full windowIds from runtime
            m_floatingWindows.insert(id);
        }

        qCDebug(lcEffect) << "Synced" << m_floatingWindows.size() << "floating windows from daemon";
    });
}

void NavigationHandler::syncFloatingStateForWindow(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }

    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        return;
    }

    // Use ASYNC D-Bus call to avoid blocking the compositor thread
    // Synchronous calls in slotWindowAdded can cause freezes during startup
    // Pass full windowId so daemon can do per-instance lookup with stableId fallback
    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("queryWindowFloating"), windowId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
        QDBusPendingReply<bool> reply = *w;
        if (reply.isValid()) {
            bool floating = reply.value();
            if (floating) {
                m_floatingWindows.insert(windowId);
                qCDebug(lcEffect) << "Synced floating state for window" << windowId << "- is floating";
            } else {
                m_floatingWindows.remove(windowId);
            }
        }
        w->deleteLater();
    });
}

} // namespace PlasmaZones
