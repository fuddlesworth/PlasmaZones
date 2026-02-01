// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "navigationhandler.h"
#include "plasmazoneseffect.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>

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

    // Check if window is floating
    if (isWindowFloating(stableId)) {
        qCDebug(lcEffect) << "Window is floating, skipping move";
        m_effect->emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("window_floating"));
        return;
    }

    // Check if this is a navigation directive
    if (targetZoneId.startsWith(NavigateDirectivePrefix)) {
        QString direction = targetZoneId.mid(NavigateDirectivePrefix.length());
        qCDebug(lcEffect) << "Navigation direction:" << direction;

        QString currentZoneId = m_effect->queryZoneForWindow(windowId);
        QString targetZone;
        QString screenName = m_effect->getWindowScreenName(activeWindow);

        if (currentZoneId.isEmpty()) {
            // Window not snapped - snap to edge zone in direction
            qCDebug(lcEffect) << "Window not snapped, finding first zone in direction" << direction;
            m_effect->ensurePreSnapGeometryStored(activeWindow, windowId);

            targetZone = m_effect->queryFirstZoneInDirection(direction);
            if (targetZone.isEmpty()) {
                qCDebug(lcEffect) << "No zones available for navigation";
                m_effect->emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_zones"));
                return;
            }
        } else {
            // Window is already snapped - navigate to adjacent zone
            targetZone = m_effect->queryAdjacentZone(currentZoneId, direction);
            if (targetZone.isEmpty()) {
                qCDebug(lcEffect) << "No adjacent zone in direction" << direction;
                m_effect->emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_adjacent_zone"));
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
        m_effect->emitNavigationFeedback(true, QStringLiteral("move"));

    } else if (!targetZoneId.isEmpty()) {
        // Direct zone ID
        QString geometryJson = zoneGeometry;
        QString screenName = m_effect->getWindowScreenName(activeWindow);

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
            m_effect->emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"));
            return;
        }

        m_effect->ensurePreSnapGeometryStored(activeWindow, windowId);
        m_effect->applySnapGeometry(activeWindow, geometry);

        auto* iface = m_effect->windowTrackingInterface();
        if (iface && iface->isValid()) {
            iface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZoneId);
        }

        m_effect->emitNavigationFeedback(true, QStringLiteral("push"));
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
    if (targetZoneId.startsWith(NavigateDirectivePrefix)) {
        QString direction = targetZoneId.mid(NavigateDirectivePrefix.length());

        KWin::EffectWindow* activeWindow = m_effect->getActiveWindow();
        if (!activeWindow) {
            return;
        }

        QString activeWindowId = m_effect->getWindowId(activeWindow);
        QString currentZoneId = m_effect->queryZoneForWindow(activeWindowId);

        if (currentZoneId.isEmpty()) {
            qCDebug(lcEffect) << "Focus navigation requires snapped window";
            m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("not_snapped"));
            return;
        }

        actualZoneId = m_effect->queryAdjacentZone(currentZoneId, direction);
        if (actualZoneId.isEmpty()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_adjacent_zone"));
            return;
        }
    }

    // Get windows in the target zone
    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        return;
    }

    QDBusMessage zoneReply = iface->call(QStringLiteral("getWindowsInZone"), actualZoneId);
    QStringList windowsInZone;
    if (zoneReply.type() == QDBusMessage::ReplyMessage && !zoneReply.arguments().isEmpty()) {
        windowsInZone = zoneReply.arguments().at(0).toStringList();
    }

    if (windowsInZone.isEmpty()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window_in_zone"));
        return;
    }

    QString targetWindowId = windowsInZone.first();
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (w && m_effect->getWindowId(w) == targetWindowId) {
            KWin::effects->activateWindow(w);
            m_effect->emitNavigationFeedback(true, QStringLiteral("focus"));
            return;
        }
    }

    m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("window_not_found"));
}

void NavigationHandler::handleRestoreWindow()
{
    qCDebug(lcEffect) << "Restore window requested";

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("restore"));
    if (!activeWindow) {
        return;
    }

    QString windowId = m_effect->getWindowId(activeWindow);
    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("restore"), QStringLiteral("dbus_error"));
        return;
    }

    QDBusMessage reply = iface->call(QStringLiteral("getValidatedPreSnapGeometry"), windowId);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 5) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("restore"), QStringLiteral("no_geometry"));
        return;
    }

    bool found = reply.arguments().at(0).toBool();
    int x = reply.arguments().at(1).toInt();
    int y = reply.arguments().at(2).toInt();
    int width = reply.arguments().at(3).toInt();
    int height = reply.arguments().at(4).toInt();

    if (!found || width <= 0 || height <= 0) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("restore"), QStringLiteral("not_snapped"));
        return;
    }

    QRect geometry(x, y, width, height);
    m_effect->applySnapGeometry(activeWindow, geometry);
    iface->asyncCall(QStringLiteral("windowUnsnapped"), windowId);
    iface->asyncCall(QStringLiteral("clearPreSnapGeometry"), windowId);

    m_effect->emitNavigationFeedback(true, QStringLiteral("restore"));
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

    bool isCurrentlyFloating = isWindowFloating(stableId);
    bool newFloatState = !isCurrentlyFloating;

    auto* iface = m_effect->windowTrackingInterface();

    if (newFloatState) {
        m_floatingWindows.insert(stableId);

        if (iface && iface->isValid()) {
            QDBusMessage msg = iface->call(QStringLiteral("getValidatedPreSnapGeometry"), windowId);
            if (msg.type() == QDBusMessage::ReplyMessage && msg.arguments().size() >= 5) {
                bool found = msg.arguments().at(0).toBool();
                if (found) {
                    int x = msg.arguments().at(1).toInt();
                    int y = msg.arguments().at(2).toInt();
                    int width = msg.arguments().at(3).toInt();
                    int height = msg.arguments().at(4).toInt();
                    if (width > 0 && height > 0) {
                        m_effect->applySnapGeometry(activeWindow, QRect(x, y, width, height));
                    }
                }
            }
            iface->asyncCall(QStringLiteral("windowUnsnappedForFloat"), windowId);
            iface->asyncCall(QStringLiteral("setWindowFloating"), windowId, true);
        }
    } else {
        m_floatingWindows.remove(stableId);

        if (iface && iface->isValid()) {
            iface->asyncCall(QStringLiteral("setWindowFloating"), windowId, false);

            QDBusMessage preMsg = iface->call(QStringLiteral("getPreFloatZone"), windowId);
            if (preMsg.type() == QDBusMessage::ReplyMessage && preMsg.arguments().size() >= 2
                && preMsg.arguments().at(0).toBool()) {
                QString zoneId = preMsg.arguments().at(1).toString();
                if (!zoneId.isEmpty()) {
                    QString screenName = m_effect->getWindowScreenName(activeWindow);
                    QString geometryJson = m_effect->queryZoneGeometryForScreen(zoneId, screenName);
                    QRect geometry = m_effect->parseZoneGeometry(geometryJson);
                    if (geometry.isValid()) {
                        m_effect->applySnapGeometry(activeWindow, geometry);
                        iface->asyncCall(QStringLiteral("windowSnapped"), windowId, zoneId);
                    }
                    iface->asyncCall(QStringLiteral("clearPreFloatZone"), windowId);
                }
            }
        }
    }

    QString actionResult = newFloatState ? QStringLiteral("floated") : QStringLiteral("unfloated");
    m_effect->emitNavigationFeedback(true, QStringLiteral("float"), actionResult);
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

    if (isWindowFloating(stableId)) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("window_floating"));
        return;
    }

    if (!targetZoneId.startsWith(SwapDirectivePrefix)) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_directive"));
        return;
    }

    QString direction = targetZoneId.mid(SwapDirectivePrefix.length());
    QString currentZoneId = m_effect->queryZoneForWindow(windowId);

    if (currentZoneId.isEmpty()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("not_snapped"));
        return;
    }

    QString targetZone = m_effect->queryAdjacentZone(currentZoneId, direction);
    if (targetZone.isEmpty()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_adjacent_zone"));
        return;
    }

    auto* iface = m_effect->windowTrackingInterface();
    QString screenName = m_effect->getWindowScreenName(activeWindow);

    QDBusMessage windowsMsg = iface->call(QStringLiteral("getWindowsInZone"), targetZone);
    QStringList windowsInTargetZone;
    if (windowsMsg.type() == QDBusMessage::ReplyMessage && !windowsMsg.arguments().isEmpty()) {
        windowsInTargetZone = windowsMsg.arguments().at(0).toStringList();
    }

    QRect targetGeom = m_effect->parseZoneGeometry(m_effect->queryZoneGeometryForScreen(targetZone, screenName));
    QRect currentGeom = m_effect->parseZoneGeometry(m_effect->queryZoneGeometryForScreen(currentZoneId, screenName));

    if (!targetGeom.isValid() || !currentGeom.isValid()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"));
        return;
    }

    if (windowsInTargetZone.isEmpty()) {
        m_effect->applySnapGeometry(activeWindow, targetGeom);
        iface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZone);
        m_effect->emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("moved_to_empty"));
    } else {
        QString targetWindowIdToSwap = windowsInTargetZone.first();

        KWin::EffectWindow* targetWindow = nullptr;
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* w : windows) {
            if (w && m_effect->getWindowId(w) == targetWindowIdToSwap) {
                targetWindow = w;
                break;
            }
        }

        if (!targetWindow || !m_effect->shouldHandleWindow(targetWindow)) {
            m_effect->applySnapGeometry(activeWindow, targetGeom);
            iface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZone);
            m_effect->emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("target_not_found"));
            return;
        }

        QString targetStableId = m_effect->extractStableId(targetWindowIdToSwap);
        if (isWindowFloating(targetStableId)) {
            m_effect->applySnapGeometry(activeWindow, targetGeom);
            iface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZone);
            m_effect->emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("target_floating"));
            return;
        }

        m_effect->ensurePreSnapGeometryStored(activeWindow, windowId);
        m_effect->ensurePreSnapGeometryStored(targetWindow, targetWindowIdToSwap);

        m_effect->applySnapGeometry(activeWindow, targetGeom);
        iface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZone);

        m_effect->applySnapGeometry(targetWindow, currentGeom);
        iface->asyncCall(QStringLiteral("windowSnapped"), targetWindowIdToSwap, currentZoneId);

        m_effect->emitNavigationFeedback(true, QStringLiteral("swap"));
    }
}

void NavigationHandler::handleRotateWindows(bool clockwise, const QString& rotationData)
{
    qCDebug(lcEffect) << "Rotate windows requested, clockwise:" << clockwise;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(rotationData.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("parse_error"));
        return;
    }

    QJsonArray rotationArray = doc.array();
    if (rotationArray.isEmpty()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_windows"));
        return;
    }

    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("dbus_error"));
        return;
    }

    QHash<QString, KWin::EffectWindow*> windowMap = m_effect->buildWindowMap();
    int successCount = 0;

    for (const QJsonValue& value : rotationArray) {
        if (!value.isObject()) {
            continue;
        }

        QJsonObject moveObj = value.toObject();
        QString windowId = moveObj[QStringLiteral("windowId")].toString();
        QString targetZoneId = moveObj[QStringLiteral("targetZoneId")].toString();
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
    }

    if (successCount > 0) {
        m_effect->emitNavigationFeedback(true, QStringLiteral("rotate"));
    } else {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_rotations"));
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
    QString currentZoneId = m_effect->queryZoneForWindow(windowId);

    if (currentZoneId.isEmpty()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("not_snapped"));
        return;
    }

    auto* iface = m_effect->windowTrackingInterface();
    QDBusMessage windowsMsg = iface->call(QStringLiteral("getWindowsInZone"), currentZoneId);
    QStringList windowIdsInZone;
    if (windowsMsg.type() == QDBusMessage::ReplyMessage && !windowsMsg.arguments().isEmpty()) {
        windowIdsInZone = windowsMsg.arguments().at(0).toStringList();
    }

    if (windowIdsInZone.size() < 2) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("single_window"));
        return;
    }

    QSet<QString> zoneWindowSet(windowIdsInZone.begin(), windowIdsInZone.end());
    QVector<KWin::EffectWindow*> sortedWindowsInZone;

    const auto stackingOrder = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : stackingOrder) {
        if (w && zoneWindowSet.contains(m_effect->getWindowId(w))) {
            sortedWindowsInZone.append(w);
        }
    }

    if (sortedWindowsInZone.size() < 2) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("single_window"));
        return;
    }

    int currentIndex = -1;
    for (int i = 0; i < sortedWindowsInZone.size(); ++i) {
        if (sortedWindowsInZone[i] == activeWindow) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex < 0) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("window_stacking_mismatch"));
        return;
    }

    int nextIndex;
    if (forward) {
        nextIndex = (currentIndex + 1) % sortedWindowsInZone.size();
    } else {
        nextIndex = (currentIndex - 1 + sortedWindowsInZone.size()) % sortedWindowsInZone.size();
    }

    KWin::EffectWindow* targetWindow = sortedWindowsInZone.at(nextIndex);
    KWin::effects->activateWindow(targetWindow);
    m_effect->emitNavigationFeedback(true, QStringLiteral("cycle"));
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

    QDBusMessage reply = iface->call(QStringLiteral("getFloatingWindows"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return;
    }

    QStringList floatingIds = reply.arguments().at(0).toStringList();
    m_floatingWindows.clear();

    for (const QString& windowId : floatingIds) {
        // Daemon now stores stableIds directly, but handle both formats for safety
        QString stableId = m_effect->extractStableId(windowId);
        m_floatingWindows.insert(stableId);
    }

    qCDebug(lcEffect) << "Synced" << m_floatingWindows.size() << "floating windows from daemon";
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
