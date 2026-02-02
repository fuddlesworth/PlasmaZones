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
#include <QDBusReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace PlasmaZones {

// Navigation directive prefixes
static const QString NavigateDirectivePrefix = QStringLiteral("navigate:");
static const QString SwapDirectivePrefix = QStringLiteral("swap:");
static const QString CycleDirectivePrefix = QStringLiteral("cycle:");

NavigationHandler::NavigationHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void NavigationHandler::handleMoveWindowToZone(const QString& targetZoneId, const QString& zoneGeometry)
{
    qCDebug(lcEffect) << "Move window to zone requested -" << targetZoneId;

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("move"));
    if (!activeWindow) {
        return;
    }

    QString windowId = m_effect->getWindowId(activeWindow);
    QString stableId = m_effect->extractStableId(windowId);
    QString screenName = m_effect->getWindowScreenName(activeWindow);

    // Check if window is floating
    if (isWindowFloating(stableId)) {
        qCDebug(lcEffect) << "Window is floating, skipping move";
        m_effect->emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("window_floating"),
                                         QString(), QString(), screenName);
        return;
    }

    // Check if this is a navigation directive
    if (targetZoneId.startsWith(NavigateDirectivePrefix)) {
        QString direction = targetZoneId.mid(NavigateDirectivePrefix.length());
        qCDebug(lcEffect) << "Navigation direction:" << direction;

        QString currentZoneId = m_effect->queryZoneForWindow(windowId);
        QString targetZone;

        if (currentZoneId.isEmpty()) {
            // Window not snapped - snap to edge zone in direction
            qCDebug(lcEffect) << "Window not snapped, finding first zone in direction" << direction;
            m_effect->ensurePreSnapGeometryStored(activeWindow, windowId);

            targetZone = m_effect->queryFirstZoneInDirection(direction);
            if (targetZone.isEmpty()) {
                qCDebug(lcEffect) << "No zones available for navigation";
                m_effect->emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_zones"),
                                                 QString(), QString(), screenName);
                return;
            }
        } else {
            // Window is already snapped - navigate to adjacent zone
            targetZone = m_effect->queryAdjacentZone(currentZoneId, direction);
            if (targetZone.isEmpty()) {
                qCDebug(lcEffect) << "No adjacent zone in direction" << direction;
                m_effect->emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_adjacent_zone"),
                                                 QString(), QString(), screenName);
                return;
            }
        }

        QString geometryJson = m_effect->queryZoneGeometryForScreen(targetZone, screenName);
        QRect geometry = m_effect->parseZoneGeometry(geometryJson);
        if (!geometry.isValid()) {
            qCDebug(lcEffect) << "Could not get valid geometry for zone" << targetZone;
            return;
        }

        m_effect->applySnapGeometry(activeWindow, geometry);
        m_effect->windowTrackingInterface()->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZone);
        m_effect->emitNavigationFeedback(true, QStringLiteral("move"), QString(), currentZoneId, targetZone, screenName);

    } else if (!targetZoneId.isEmpty()) {
        // Direct zone ID
        QString geometryJson = zoneGeometry;

        if (!screenName.isEmpty()) {
            QString screenGeometry = m_effect->queryZoneGeometryForScreen(targetZoneId, screenName);
            if (!screenGeometry.isEmpty()) {
                geometryJson = screenGeometry;
            }
        }

        if (geometryJson.isEmpty()) {
            geometryJson = m_effect->queryZoneGeometry(targetZoneId);
        }

        QRect geometry = m_effect->parseZoneGeometry(geometryJson);
        if (!geometry.isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"),
                                             QString(), QString(), screenName);
            return;
        }

        m_effect->ensurePreSnapGeometryStored(activeWindow, windowId);
        m_effect->applySnapGeometry(activeWindow, geometry);

        auto* iface = m_effect->windowTrackingInterface();
        if (iface && iface->isValid()) {
            iface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZoneId);
        }

        m_effect->emitNavigationFeedback(true, QStringLiteral("push"), QString(), QString(), targetZoneId, screenName);
    }
}

void NavigationHandler::handleFocusWindowInZone(const QString& targetZoneId, const QString& windowId)
{
    Q_UNUSED(windowId)
    qCDebug(lcEffect) << "Focus window in zone requested -" << targetZoneId;

    if (targetZoneId.isEmpty()) {
        return;
    }

    QString actualZoneId = targetZoneId;
    QString sourceZoneIdForOsd; // For OSD highlighting: zone we're focusing FROM
    QString screenName; // For OSD placement

    if (targetZoneId.startsWith(NavigateDirectivePrefix)) {
        QString direction = targetZoneId.mid(NavigateDirectivePrefix.length());

        KWin::EffectWindow* activeWindow = m_effect->getActiveWindow();
        if (!activeWindow) {
            return;
        }

        screenName = m_effect->getWindowScreenName(activeWindow);
        QString activeWindowId = m_effect->getWindowId(activeWindow);
        QString currentZoneId = m_effect->queryZoneForWindow(activeWindowId);

        if (currentZoneId.isEmpty()) {
            qCDebug(lcEffect) << "Focus navigation requires snapped window";
            m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("not_snapped"),
                                             QString(), QString(), screenName);
            return;
        }

        sourceZoneIdForOsd = currentZoneId;
        actualZoneId = m_effect->queryAdjacentZone(currentZoneId, direction);
        if (actualZoneId.isEmpty()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_adjacent_zone"),
                                             QString(), QString(), screenName);
            return;
        }
    }

    // Use ASYNC D-Bus call to get windows in zone
    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        return;
    }

    // Capture zone IDs and screen for async callback
    QString capturedSourceZoneId = sourceZoneIdForOsd;
    QString capturedActualZoneId = actualZoneId;
    QString capturedScreenName = screenName;

    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getWindowsInZone"), actualZoneId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, capturedSourceZoneId, capturedActualZoneId, capturedScreenName](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QStringList> reply = *w;
        if (!reply.isValid() || reply.value().isEmpty()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window_in_zone"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        QStringList windowsInZone = reply.value();
        QString targetWindowId = windowsInZone.first();
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* win : windows) {
            if (win && m_effect->getWindowId(win) == targetWindowId) {
                KWin::effects->activateWindow(win);
                m_effect->emitNavigationFeedback(true, QStringLiteral("focus"), QString(),
                                                 capturedSourceZoneId, capturedActualZoneId, capturedScreenName);
                return;
            }
        }

        m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("window_not_found"),
                                         QString(), QString(), capturedScreenName);
    });
}

void NavigationHandler::handleRestoreWindow()
{
    qCDebug(lcEffect) << "Restore window requested";

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("restore"));
    if (!activeWindow) {
        return;
    }

    QString windowId = m_effect->getWindowId(activeWindow);
    QString screenName = m_effect->getWindowScreenName(activeWindow);
    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("restore"), QStringLiteral("dbus_error"),
                                         QString(), QString(), screenName);
        return;
    }

    // Use QPointer to safely handle window destruction during async call
    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    QString capturedWindowId = windowId;
    QString capturedScreenName = screenName;

    // Use ASYNC D-Bus call to get validated pre-snap geometry
    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getValidatedPreSnapGeometry"), windowId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, capturedWindowId, capturedScreenName](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<bool, int, int, int, int> reply = *w;
        if (!reply.isValid() || reply.count() < 5) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("restore"), QStringLiteral("no_geometry"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        bool found = reply.argumentAt<0>();
        int x = reply.argumentAt<1>();
        int y = reply.argumentAt<2>();
        int width = reply.argumentAt<3>();
        int height = reply.argumentAt<4>();

        if (!found || width <= 0 || height <= 0) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("restore"), QStringLiteral("not_snapped"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        if (!safeWindow) {
            qCDebug(lcEffect) << "Window was destroyed during async call";
            return;
        }

        QRect geometry(x, y, width, height);
        m_effect->applySnapGeometry(safeWindow, geometry);

        auto* iface = m_effect->windowTrackingInterface();
        if (iface && iface->isValid()) {
            iface->asyncCall(QStringLiteral("windowUnsnapped"), capturedWindowId);
            iface->asyncCall(QStringLiteral("clearPreSnapGeometry"), capturedWindowId);
        }

        m_effect->emitNavigationFeedback(true, QStringLiteral("restore"), QString(),
                                         QString(), QString(), capturedScreenName);
    });
}

void NavigationHandler::handleToggleWindowFloat(bool shouldFloat)
{
    Q_UNUSED(shouldFloat)
    qCDebug(lcEffect) << "Toggle float requested";

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("float"));
    if (!activeWindow) {
        return;
    }

    QString windowId = m_effect->getWindowId(activeWindow);
    QString stableId = m_effect->extractStableId(windowId);
    QString screenName = m_effect->getWindowScreenName(activeWindow);

    bool isCurrentlyFloating = isWindowFloating(stableId);
    bool newFloatState = !isCurrentlyFloating;

    auto* iface = m_effect->windowTrackingInterface();

    if (newFloatState) {
        // Floating ON - restore pre-snap geometry and mark as floating
        m_floatingWindows.insert(stableId);

        if (iface && iface->isValid()) {
            // Use QPointer for safe async handling
            QPointer<KWin::EffectWindow> safeWindow = activeWindow;
            QString capturedWindowId = windowId;

            // Use ASYNC D-Bus call to get validated pre-snap geometry
            QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getValidatedPreSnapGeometry"), windowId);
            auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

            connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, safeWindow, capturedWindowId](QDBusPendingCallWatcher* w) {
                w->deleteLater();

                auto* iface = m_effect->windowTrackingInterface();
                if (!iface || !iface->isValid()) {
                    return;
                }

                QDBusPendingReply<bool, int, int, int, int> reply = *w;
                if (reply.isValid() && reply.count() >= 5 && safeWindow) {
                    bool found = reply.argumentAt<0>();
                    if (found) {
                        int x = reply.argumentAt<1>();
                        int y = reply.argumentAt<2>();
                        int width = reply.argumentAt<3>();
                        int height = reply.argumentAt<4>();
                        if (width > 0 && height > 0) {
                            m_effect->applySnapGeometry(safeWindow, QRect(x, y, width, height));
                        }
                    }
                }

                iface->asyncCall(QStringLiteral("windowUnsnappedForFloat"), capturedWindowId);
                iface->asyncCall(QStringLiteral("setWindowFloating"), capturedWindowId, true);
            });
        }

        m_effect->emitNavigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"),
                                         QString(), QString(), screenName);
    } else {
        // Floating OFF - restore to previous zone if available
        m_floatingWindows.remove(stableId);

        if (iface && iface->isValid()) {
            iface->asyncCall(QStringLiteral("setWindowFloating"), windowId, false);

            // Use QPointer for safe async handling
            QPointer<KWin::EffectWindow> safeWindow = activeWindow;
            QString capturedWindowId = windowId;

            // Use ASYNC D-Bus call to get pre-float zone
            QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getPreFloatZone"), windowId);
            auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

            connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, safeWindow, capturedWindowId](QDBusPendingCallWatcher* w) {
                w->deleteLater();

                QDBusPendingReply<bool, QString> reply = *w;
                if (!reply.isValid() || reply.count() < 2 || !reply.argumentAt<0>()) {
                    return;
                }

                if (!safeWindow) {
                    qCDebug(lcEffect) << "Window was destroyed during async call";
                    return;
                }

                QString zoneId = reply.argumentAt<1>();
                if (zoneId.isEmpty()) {
                    return;
                }

                QString screenName = m_effect->getWindowScreenName(safeWindow);
                QString geometryJson = m_effect->queryZoneGeometryForScreen(zoneId, screenName);
                QRect geometry = m_effect->parseZoneGeometry(geometryJson);
                if (geometry.isValid()) {
                    m_effect->applySnapGeometry(safeWindow, geometry);
                    auto* iface = m_effect->windowTrackingInterface();
                    if (iface && iface->isValid()) {
                        iface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, zoneId);
                        iface->asyncCall(QStringLiteral("clearPreFloatZone"), capturedWindowId);
                    }
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
    qCDebug(lcEffect) << "Swap windows requested -" << targetZoneId;

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("swap"));
    if (!activeWindow) {
        return;
    }

    QString windowId = m_effect->getWindowId(activeWindow);
    QString stableId = m_effect->extractStableId(windowId);
    QString screenName = m_effect->getWindowScreenName(activeWindow);

    if (isWindowFloating(stableId)) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("window_floating"),
                                         QString(), QString(), screenName);
        return;
    }

    if (!targetZoneId.startsWith(SwapDirectivePrefix)) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_directive"),
                                         QString(), QString(), screenName);
        return;
    }

    QString direction = targetZoneId.mid(SwapDirectivePrefix.length());
    QString currentZoneId = m_effect->queryZoneForWindow(windowId);

    if (currentZoneId.isEmpty()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("not_snapped"),
                                         QString(), QString(), screenName);
        return;
    }

    QString targetZone = m_effect->queryAdjacentZone(currentZoneId, direction);
    if (targetZone.isEmpty()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_adjacent_zone"),
                                         QString(), QString(), screenName);
        return;
    }

    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("dbus_error"),
                                         QString(), QString(), screenName);
        return;
    }

    // Capture all needed data for async callback
    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    QString capturedWindowId = windowId;
    QString capturedCurrentZoneId = currentZoneId;
    QString capturedTargetZone = targetZone;
    QString capturedStableId = stableId;
    QString capturedScreenName = screenName;

    // Use ASYNC D-Bus call to get windows in target zone
    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getWindowsInZone"), targetZone);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, capturedWindowId, capturedCurrentZoneId, capturedTargetZone, capturedStableId, capturedScreenName](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        if (!safeWindow) {
            qCDebug(lcEffect) << "Window was destroyed during async call";
            return;
        }

        QDBusPendingReply<QStringList> reply = *w;
        QStringList windowsInTargetZone;
        if (reply.isValid()) {
            windowsInTargetZone = reply.value();
        }

        QRect targetGeom = m_effect->parseZoneGeometry(m_effect->queryZoneGeometryForScreen(capturedTargetZone, capturedScreenName));
        QRect currentGeom = m_effect->parseZoneGeometry(m_effect->queryZoneGeometryForScreen(capturedCurrentZoneId, capturedScreenName));

        if (!targetGeom.isValid() || !currentGeom.isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        auto* iface = m_effect->windowTrackingInterface();
        if (!iface || !iface->isValid()) {
            return;
        }

        if (windowsInTargetZone.isEmpty()) {
            m_effect->applySnapGeometry(safeWindow, targetGeom);
            iface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZone);
            m_effect->emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("moved_to_empty"),
                                             capturedCurrentZoneId, capturedTargetZone, capturedScreenName);
        } else {
            QString targetWindowIdToSwap = windowsInTargetZone.first();

            KWin::EffectWindow* targetWindow = nullptr;
            const auto windows = KWin::effects->stackingOrder();
            for (KWin::EffectWindow* win : windows) {
                if (win && m_effect->getWindowId(win) == targetWindowIdToSwap) {
                    targetWindow = win;
                    break;
                }
            }

            if (!targetWindow || !m_effect->shouldHandleWindow(targetWindow)) {
                m_effect->applySnapGeometry(safeWindow, targetGeom);
                iface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZone);
                m_effect->emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("target_not_found"),
                                                 capturedCurrentZoneId, capturedTargetZone, capturedScreenName);
                return;
            }

            QString targetStableId = m_effect->extractStableId(targetWindowIdToSwap);
            if (isWindowFloating(targetStableId)) {
                m_effect->applySnapGeometry(safeWindow, targetGeom);
                iface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZone);
                m_effect->emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("target_floating"),
                                                 capturedCurrentZoneId, capturedTargetZone, capturedScreenName);
                return;
            }

            m_effect->ensurePreSnapGeometryStored(safeWindow, capturedWindowId);
            m_effect->ensurePreSnapGeometryStored(targetWindow, targetWindowIdToSwap);

            m_effect->applySnapGeometry(safeWindow, targetGeom);
            iface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZone);

            m_effect->applySnapGeometry(targetWindow, currentGeom);
            iface->asyncCall(QStringLiteral("windowSnapped"), targetWindowIdToSwap, capturedCurrentZoneId);

            // For swap, highlight both source and target zones
            m_effect->emitNavigationFeedback(true, QStringLiteral("swap"), QString(),
                                             capturedCurrentZoneId, capturedTargetZone, capturedScreenName);
        }
    });
}

void NavigationHandler::handleRotateWindows(bool clockwise, const QString& rotationData)
{
    qCDebug(lcEffect) << "Rotate windows requested, clockwise:" << clockwise;

    // Get screen name from active window for OSD placement
    KWin::EffectWindow* activeWindow = m_effect->getActiveWindow();
    QString screenName = activeWindow ? m_effect->getWindowScreenName(activeWindow) : QString();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(rotationData.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("parse_error"),
                                         QString(), QString(), screenName);
        return;
    }

    QJsonArray rotationArray = doc.array();
    if (rotationArray.isEmpty()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_windows"),
                                         QString(), QString(), screenName);
        return;
    }

    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("dbus_error"),
                                         QString(), QString(), screenName);
        return;
    }

    QHash<QString, KWin::EffectWindow*> windowMap = m_effect->buildWindowMap();
    int successCount = 0;
    QString firstSourceZoneId; // Capture first move's source zone for OSD
    QString firstTargetZoneId; // Capture first move's target zone for OSD

    for (const QJsonValue& value : rotationArray) {
        if (!value.isObject()) {
            continue;
        }

        QJsonObject moveObj = value.toObject();
        QString windowId = moveObj[QStringLiteral("windowId")].toString();
        QString targetZoneId = moveObj[QStringLiteral("targetZoneId")].toString();
        QString sourceZoneId = moveObj[QStringLiteral("sourceZoneId")].toString();
        int x = moveObj[QStringLiteral("x")].toInt();
        int y = moveObj[QStringLiteral("y")].toInt();
        int width = moveObj[QStringLiteral("width")].toInt();
        int height = moveObj[QStringLiteral("height")].toInt();

        if (windowId.isEmpty() || targetZoneId.isEmpty()) {
            continue;
        }

        QString stableId = m_effect->extractStableId(windowId);
        KWin::EffectWindow* window = windowMap.value(stableId);

        if (!window) {
            continue;
        }

        if (isWindowFloating(stableId)) {
            continue;
        }

        m_effect->ensurePreSnapGeometryStored(window, windowId);
        m_effect->applySnapGeometry(window, QRect(x, y, width, height));
        iface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZoneId);
        ++successCount;

        // Capture first successful move's zones for OSD highlighting
        if (successCount == 1) {
            firstSourceZoneId = sourceZoneId;
            firstTargetZoneId = targetZoneId;
        }
    }

    if (successCount > 0) {
        // Pass direction and count in reason field for OSD display
        // Format: "clockwise:N" or "counterclockwise:N" where N is window count
        QString direction = clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise");
        QString reason = QStringLiteral("%1:%2").arg(direction).arg(successCount);
        m_effect->emitNavigationFeedback(true, QStringLiteral("rotate"), reason,
                                         firstSourceZoneId, firstTargetZoneId, screenName);
    } else {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_rotations"),
                                         QString(), QString(), screenName);
    }
}

void NavigationHandler::handleCycleWindowsInZone(const QString& directive, const QString& unused)
{
    Q_UNUSED(unused)
    qCDebug(lcEffect) << "Cycle windows in zone requested -" << directive;

    if (!directive.startsWith(CycleDirectivePrefix)) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("invalid_directive"));
        return;
    }

    QString direction = directive.mid(CycleDirectivePrefix.length());
    bool forward;
    if (direction == QStringLiteral("forward")) {
        forward = true;
    } else if (direction == QStringLiteral("backward")) {
        forward = false;
    } else {
        m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("invalid_direction"));
        return;
    }

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("cycle"));
    if (!activeWindow) {
        return;
    }

    QString windowId = m_effect->getWindowId(activeWindow);
    QString screenName = m_effect->getWindowScreenName(activeWindow);
    QString currentZoneId = m_effect->queryZoneForWindow(windowId);

    if (currentZoneId.isEmpty()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("not_snapped"),
                                         QString(), QString(), screenName);
        return;
    }

    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("dbus_error"),
                                         QString(), QString(), screenName);
        return;
    }

    // Capture data for async callback
    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    bool capturedForward = forward;
    QString capturedCurrentZoneId = currentZoneId;
    QString capturedScreenName = screenName;

    // Use ASYNC D-Bus call to get windows in zone
    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getWindowsInZone"), currentZoneId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, capturedForward, capturedCurrentZoneId, capturedScreenName](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        if (!safeWindow) {
            qCDebug(lcEffect) << "Window was destroyed during async call";
            return;
        }

        QDBusPendingReply<QStringList> reply = *w;
        if (!reply.isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("dbus_error"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        QStringList windowIdsInZone = reply.value();

        if (windowIdsInZone.size() < 2) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("single_window"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        QSet<QString> zoneWindowSet(windowIdsInZone.begin(), windowIdsInZone.end());
        QVector<KWin::EffectWindow*> sortedWindowsInZone;

        const auto stackingOrder = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* win : stackingOrder) {
            if (win && zoneWindowSet.contains(m_effect->getWindowId(win))) {
                sortedWindowsInZone.append(win);
            }
        }

        if (sortedWindowsInZone.size() < 2) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("single_window"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        int currentIndex = -1;
        for (int i = 0; i < sortedWindowsInZone.size(); ++i) {
            if (sortedWindowsInZone[i] == safeWindow) {
                currentIndex = i;
                break;
            }
        }

        if (currentIndex < 0) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("window_stacking_mismatch"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        int nextIndex;
        if (capturedForward) {
            nextIndex = (currentIndex + 1) % sortedWindowsInZone.size();
        } else {
            nextIndex = (currentIndex - 1 + sortedWindowsInZone.size()) % sortedWindowsInZone.size();
        }

        KWin::EffectWindow* targetWindow = sortedWindowsInZone.at(nextIndex);
        KWin::effects->activateWindow(targetWindow);
        // For cycle, highlight the current zone (same source and target)
        m_effect->emitNavigationFeedback(true, QStringLiteral("cycle"), QString(),
                                         capturedCurrentZoneId, capturedCurrentZoneId, capturedScreenName);
    });
}

bool NavigationHandler::isWindowFloating(const QString& stableId) const
{
    return m_floatingWindows.contains(stableId);
}

void NavigationHandler::setWindowFloating(const QString& stableId, bool floating)
{
    if (floating) {
        m_floatingWindows.insert(stableId);
    } else {
        m_floatingWindows.remove(stableId);
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

        for (const QString& windowId : floatingIds) {
            // Daemon now stores stableIds directly, but handle both formats for safety
            QString stableId = m_effect->extractStableId(windowId);
            m_floatingWindows.insert(stableId);
        }

        qCDebug(lcEffect) << "Synced" << m_floatingWindows.size() << "floating windows from daemon";
    });
}

void NavigationHandler::syncFloatingStateForWindow(const QString& stableId)
{
    if (stableId.isEmpty()) {
        return;
    }

    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        return;
    }

    // Use ASYNC D-Bus call to avoid blocking the compositor thread
    // Synchronous calls in slotWindowAdded can cause freezes during startup
    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("queryWindowFloating"), stableId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, stableId](QDBusPendingCallWatcher* w) {
        QDBusPendingReply<bool> reply = *w;
        if (reply.isValid()) {
            bool isFloating = reply.value();
            if (isFloating) {
                m_floatingWindows.insert(stableId);
                qCDebug(lcEffect) << "Synced floating state for window" << stableId << "- is floating";
            } else {
                m_floatingWindows.remove(stableId);
            }
        }
        w->deleteLater();
    });
}

} // namespace PlasmaZones
