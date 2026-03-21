// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "navigationhandler.h"
#include "autotilehandler.h"
#include "plasmazoneseffect.h"
#include "snapassisthandler.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

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

QDBusInterface* NavigationHandler::requireInterface(const QString& action, const QString& screenId)
{
    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        m_effect->emitNavigationFeedback(false, action, QStringLiteral("dbus_error"), QString(), QString(), screenId);
        return nullptr;
    }
    return iface;
}

void NavigationHandler::applyDaemonSnapReply(QDBusPendingCallWatcher* watcher, QPointer<KWin::EffectWindow> safeWindow,
                                             const QString& windowId, const QString& screenId,
                                             const QRectF& preSnapGeom, const QString& action)
{
    watcher->deleteLater();
    if (!safeWindow)
        return;

    QDBusPendingReply<QString> reply = *watcher;
    if (!reply.isValid()) {
        m_effect->emitNavigationFeedback(false, action, QStringLiteral("dbus_error"), QString(), QString(), screenId);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
    if (!doc.isObject()) {
        m_effect->emitNavigationFeedback(false, action, QStringLiteral("parse_error"), QString(), QString(), screenId);
        return;
    }

    QJsonObject obj = doc.object();
    if (!obj.value(QLatin1String("success")).toBool(false))
        return;

    QString zoneId = obj.value(QLatin1String("zoneId")).toString();
    QString geometryJson = obj.value(QLatin1String("geometryJson")).toString();
    QString sourceZoneId = obj.value(QLatin1String("sourceZoneId")).toString();
    // Prefer the daemon's authoritative screen over the effect-captured one.
    // The daemon resolves the stored screen assignment for snapped windows,
    // which may differ from what KWin reports (multi-monitor edge cases).
    QString effectiveScreen = obj.value(QLatin1String("screenName")).toString();
    if (effectiveScreen.isEmpty()) {
        effectiveScreen = screenId;
    }

    QRect geometry = m_effect->parseZoneGeometry(geometryJson);
    if (!geometry.isValid())
        return;

    if (sourceZoneId.isEmpty()) {
        m_effect->ensurePreSnapGeometryStored(safeWindow, windowId, preSnapGeom);
    }

    m_effect->applySnapGeometry(safeWindow, geometry);
    auto* snapIface = m_effect->windowTrackingInterface();
    if (snapIface && snapIface->isValid()) {
        snapIface->asyncCall(QStringLiteral("windowSnapped"), windowId, zoneId, effectiveScreen);
        snapIface->asyncCall(QStringLiteral("recordSnapIntent"), windowId, true);
    }
}

void NavigationHandler::emitBatchFeedback(const BatchSnapResult& result, const QString& action, const QString& screenId)
{
    switch (result.status) {
    case BatchSnapResult::ParseError:
        m_effect->emitNavigationFeedback(false, action, QStringLiteral("parse_error"), QString(), QString(), screenId);
        break;
    case BatchSnapResult::EmptyData:
        m_effect->emitNavigationFeedback(false, action, QStringLiteral("no_windows"), QString(), QString(), screenId);
        break;
    case BatchSnapResult::DbusError:
        m_effect->emitNavigationFeedback(false, action, QStringLiteral("dbus_error"), QString(), QString(), screenId);
        break;
    case BatchSnapResult::Success:
        break; // Success/zero-match handling is caller-specific
    }
}

void NavigationHandler::handleMoveWindowToZone(const QString& targetZoneId, const QString& zoneGeometry)
{
    qCInfo(lcEffect) << "Move window to zone requested -" << targetZoneId;

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("move"));
    if (!activeWindow) {
        return;
    }

    QString windowId = m_effect->getWindowId(activeWindow);
    QString screenId = m_effect->getWindowScreenId(activeWindow);

    // User-initiated snap commands override floating state.
    // windowSnapped() on the daemon will clear floating via clearFloatingStateForSnap().

    if (targetZoneId.startsWith(NavigateDirectivePrefix)) {
        handleNavigateMove(activeWindow, windowId, screenId, targetZoneId.mid(NavigateDirectivePrefix.length()));
    } else if (targetZoneId == PushDirective) {
        handlePushMove(activeWindow, windowId, screenId, zoneGeometry.isEmpty() ? screenId : zoneGeometry);
    } else if (targetZoneId.startsWith(SnapDirectivePrefix)) {
        bool ok = false;
        int zoneNumber = targetZoneId.mid(SnapDirectivePrefix.length()).toInt(&ok);
        if (!ok || zoneNumber < 1 || zoneNumber > 9) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"),
                                             QString(), QString(), screenId);
            return;
        }
        handleSnapByNumber(activeWindow, windowId, screenId, zoneNumber,
                           zoneGeometry.isEmpty() ? screenId : zoneGeometry);
    } else if (!targetZoneId.isEmpty()) {
        handleDirectZoneSnap(activeWindow, windowId, screenId, targetZoneId, zoneGeometry);
    }
}

void NavigationHandler::handleNavigateMove(KWin::EffectWindow* activeWindow, const QString& windowId,
                                           const QString& screenId, const QString& direction)
{
    qCDebug(lcEffect) << "Navigation direction (daemon-driven):" << direction;

    auto* iface = requireInterface(QStringLiteral("move"), screenId);
    if (!iface)
        return;

    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    QRectF preSnapGeom = activeWindow->frameGeometry();

    QDBusPendingCall pendingCall =
        iface->asyncCall(QStringLiteral("getMoveTargetForWindow"), windowId, direction, screenId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, windowId, screenId, preSnapGeom](QDBusPendingCallWatcher* w) {
                applyDaemonSnapReply(w, safeWindow, windowId, screenId, preSnapGeom, QStringLiteral("move"));
            });
}

void NavigationHandler::handlePushMove(KWin::EffectWindow* activeWindow, const QString& windowId,
                                       const QString& /*screenId*/, const QString& pushScreenId)
{
    auto* iface = requireInterface(QStringLiteral("push"), pushScreenId);
    if (!iface)
        return;

    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    QRectF preSnapGeom = activeWindow->frameGeometry();

    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getPushTargetForWindow"), windowId, pushScreenId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, windowId, pushScreenId, preSnapGeom](QDBusPendingCallWatcher* w) {
                applyDaemonSnapReply(w, safeWindow, windowId, pushScreenId, preSnapGeom, QStringLiteral("push"));
            });
}

void NavigationHandler::handleSnapByNumber(KWin::EffectWindow* activeWindow, const QString& windowId,
                                           const QString& /*screenId*/, int zoneNumber, const QString& snapScreenId)
{
    auto* iface = requireInterface(QStringLiteral("snap"), snapScreenId);
    if (!iface)
        return;

    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    QRectF preSnapGeom = activeWindow->frameGeometry();

    QDBusPendingCall pendingCall =
        iface->asyncCall(QStringLiteral("getSnapToZoneByNumberTarget"), windowId, zoneNumber, snapScreenId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, windowId, snapScreenId, preSnapGeom](QDBusPendingCallWatcher* w) {
                applyDaemonSnapReply(w, safeWindow, windowId, snapScreenId, preSnapGeom, QStringLiteral("snap"));
            });
}

void NavigationHandler::handleDirectZoneSnap(KWin::EffectWindow* activeWindow, const QString& windowId,
                                             const QString& screenId, const QString& targetZoneId,
                                             const QString& zoneGeometry)
{
    // Direct zone ID (legacy — e.g. from moveSpecificWindowToZoneRequested for snap assist).
    // The daemon provides geometry but it may be for the primary screen.
    // Use ASYNC D-Bus call to get screen-specific geometry.
    auto* iface = requireInterface(QStringLiteral("push"), screenId);
    if (!iface)
        return;

    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    QRectF preSnapGeom = activeWindow->frameGeometry();
    bool hasValidPreSnapGeom = preSnapGeom.width() > 0 && preSnapGeom.height() > 0;

    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getZoneGeometryForScreen"), targetZoneId, screenId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, windowId, targetZoneId, screenId, zoneGeometry, preSnapGeom,
             hasValidPreSnapGeom](QDBusPendingCallWatcher* w) {
                w->deleteLater();

                if (!safeWindow) {
                    m_effect->emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("window_destroyed"),
                                                     QString(), QString(), screenId);
                    return;
                }

                QDBusPendingReply<QString> reply = *w;
                QString geometryJson = reply.isValid() && !reply.value().isEmpty() ? reply.value() : zoneGeometry;

                QRect geometry = m_effect->parseZoneGeometry(geometryJson);
                if (!geometry.isValid()) {
                    m_effect->emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"),
                                                     QString(), QString(), screenId);
                    return;
                }

                if (hasValidPreSnapGeom) {
                    auto* innerIface = m_effect->windowTrackingInterface();
                    if (innerIface && innerIface->isValid()) {
                        QDBusPendingCall hasGeomCall =
                            innerIface->asyncCall(QStringLiteral("hasPreTileGeometry"), windowId);
                        auto* hasGeomWatcher = new QDBusPendingCallWatcher(hasGeomCall, this);
                        connect(hasGeomWatcher, &QDBusPendingCallWatcher::finished, this,
                                [this, windowId, preSnapGeom, screenId](QDBusPendingCallWatcher* hgw) {
                                    hgw->deleteLater();
                                    QDBusPendingReply<bool> hasGeomReply = *hgw;
                                    if (!hasGeomReply.isValid() || !hasGeomReply.value()) {
                                        auto* storeIface = m_effect->windowTrackingInterface();
                                        if (storeIface && storeIface->isValid()) {
                                            storeIface->asyncCall(
                                                QStringLiteral("storePreTileGeometry"), windowId,
                                                static_cast<int>(preSnapGeom.x()), static_cast<int>(preSnapGeom.y()),
                                                static_cast<int>(preSnapGeom.width()),
                                                static_cast<int>(preSnapGeom.height()), screenId, false);
                                        }
                                    }
                                });
                    }
                }

                m_effect->applySnapGeometry(safeWindow, geometry);
                auto* snapIface = m_effect->windowTrackingInterface();
                if (snapIface && snapIface->isValid()) {
                    snapIface->asyncCall(QStringLiteral("windowSnapped"), windowId, targetZoneId, screenId);
                }

                m_effect->emitNavigationFeedback(true, QStringLiteral("push"), QString(), QString(), targetZoneId,
                                                 screenId);
            });
}

void NavigationHandler::handleFocusWindowInZone(const QString& targetZoneId, const QString& windowId)
{
    Q_UNUSED(windowId)
    qCInfo(lcEffect) << "Focus window in zone requested -" << targetZoneId;

    if (targetZoneId.isEmpty()) {
        return;
    }

    QString screenId; // For OSD placement (EDID-based screen ID)

    // Default screen from active window (used when targetZoneId is a direct zone ID)
    KWin::EffectWindow* activeWindow = m_effect->getActiveWindow();
    if (activeWindow) {
        screenId = m_effect->getWindowScreenId(activeWindow);
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
                        m_effect->emitNavigationFeedback(false, QStringLiteral("focus"),
                                                         QStringLiteral("no_window_in_zone"), QString(), QString(),
                                                         screen);
                        return;
                    }

                    QStringList windowsInZone = reply.value();
                    QString targetWindowId = windowsInZone.first();
                    KWin::EffectWindow* win = m_effect->findWindowById(targetWindowId);
                    if (win) {
                        KWin::effects->activateWindow(win);
                        m_effect->emitNavigationFeedback(true, QStringLiteral("focus"), QString(), sourceZoneId, zoneId,
                                                         screen);
                        return;
                    }

                    m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("window_not_found"),
                                                     QString(), QString(), screen);
                });
    };

    if (targetZoneId.startsWith(NavigateDirectivePrefix)) {
        QString direction = targetZoneId.mid(NavigateDirectivePrefix.length());
        if (!activeWindow)
            return;

        QString activeWindowId = m_effect->getWindowId(activeWindow);
        QString capturedScreenId = screenId;

        auto* iface = m_effect->windowTrackingInterface();
        if (!iface || !iface->isValid())
            return;

        QDBusPendingCall pendingCall =
            iface->asyncCall(QStringLiteral("getFocusTargetForWindow"), activeWindowId, direction, capturedScreenId);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, capturedScreenId](QDBusPendingCallWatcher* w) {
                    w->deleteLater();

                    QDBusPendingReply<QString> reply = *w;
                    if (!reply.isValid())
                        return;

                    QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
                    if (!doc.isObject())
                        return;

                    QJsonObject obj = doc.object();
                    if (!obj.value(QLatin1String("success")).toBool(false))
                        return;

                    QString windowIdToActivate = obj.value(QLatin1String("windowIdToActivate")).toString();
                    if (windowIdToActivate.isEmpty())
                        return;

                    KWin::EffectWindow* win = m_effect->findWindowById(windowIdToActivate);
                    if (win) {
                        KWin::effects->activateWindow(win);
                    }
                });
    } else {
        // Direct zone ID - proceed directly to async getWindowsInZone
        doFocusInZone(QString(), targetZoneId, screenId);
    }
}

void NavigationHandler::handleRestoreWindow()
{
    qCInfo(lcEffect) << "Restore window requested (daemon-driven)";

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("restore"));
    if (!activeWindow)
        return;

    QString windowId = m_effect->getWindowId(activeWindow);
    QString screenId = m_effect->getWindowScreenId(activeWindow);
    auto* iface = requireInterface(QStringLiteral("restore"), screenId);
    if (!iface)
        return;

    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    QString capturedWindowId = windowId;
    QString capturedScreenId = screenId;

    QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getRestoreForWindow"), windowId, screenId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, capturedWindowId, capturedScreenId](QDBusPendingCallWatcher* w) {
                w->deleteLater();

                if (!safeWindow)
                    return;

                QDBusPendingReply<QString> reply = *w;
                if (!reply.isValid())
                    return;

                QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
                if (!doc.isObject())
                    return;

                QJsonObject obj = doc.object();
                if (!obj.value(QLatin1String("success")).toBool(false))
                    return;

                int x = obj.value(QLatin1String("x")).toInt();
                int y = obj.value(QLatin1String("y")).toInt();
                int width = obj.value(QLatin1String("width")).toInt();
                int height = obj.value(QLatin1String("height")).toInt();
                if (width <= 0 || height <= 0)
                    return;

                QRect geometry(x, y, width, height);
                m_effect->applySnapGeometry(safeWindow, geometry);

                auto* innerIface = m_effect->windowTrackingInterface();
                if (innerIface && innerIface->isValid()) {
                    innerIface->asyncCall(QStringLiteral("windowUnsnapped"), capturedWindowId);
                    innerIface->asyncCall(QStringLiteral("clearPreTileGeometry"), capturedWindowId);
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
    QString screenId = m_effect->getWindowScreenId(activeWindow);

    auto* iface = m_effect->windowTrackingInterface();

    // Query daemon's floating state async to ensure local cache is in sync.
    // This fixes race conditions where windowFloatingChanged signal hasn't arrived yet
    // (e.g., after drag-snapping a floating window).
    // Use full windowId so the daemon can distinguish multiple instances of the same app.
    if (iface && iface->isValid()) {
        QPointer<KWin::EffectWindow> safeWindow = activeWindow;
        QString capturedWindowId = windowId;
        QString capturedScreenId = screenId;

        QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("queryWindowFloating"), windowId);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeWindow, capturedWindowId, capturedScreenId](QDBusPendingCallWatcher* w) {
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
                            qCDebug(lcEffect)
                                << "Syncing floating state from daemon: windowId=" << capturedWindowId
                                << "local=" << isWindowFloating(capturedWindowId) << "daemon=" << daemonFloating;
                            setWindowFloating(capturedWindowId, daemonFloating);
                        }
                    }

                    // Now perform the toggle with accurate state
                    bool newFloatState = !daemonFloating;
                    executeFloatToggle(safeWindow, capturedWindowId, capturedScreenId, newFloatState);
                });
        return; // Actual toggle happens in async callback
    }

    // Fallback if D-Bus not available - use local cache
    bool isCurrentlyFloating = isWindowFloating(windowId);
    bool newFloatState = !isCurrentlyFloating;

    executeFloatToggle(activeWindow, windowId, screenId, newFloatState);
}

void NavigationHandler::executeFloatToggle(KWin::EffectWindow* activeWindow, const QString& windowId,
                                           const QString& screenId, bool newFloatState)
{
    if (newFloatState) {
        executeFloatOn(activeWindow, windowId, screenId);
    } else {
        executeFloatOff(activeWindow, windowId, screenId);
    }
}

void NavigationHandler::executeFloatOn(KWin::EffectWindow* activeWindow, const QString& windowId,
                                       const QString& screenId)
{
    m_floatingWindows.insert(windowId);
    qCInfo(lcEffect) << "Floating window:" << windowId;

    auto* iface = m_effect->windowTrackingInterface();
    if (iface && iface->isValid()) {
        QPointer<KWin::EffectWindow> safeWindow = activeWindow;

        QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getValidatedPreTileGeometry"), windowId);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeWindow, windowId, screenId](QDBusPendingCallWatcher* w) {
                    w->deleteLater();

                    auto* iface = m_effect->windowTrackingInterface();

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

                    if (iface && iface->isValid()) {
                        iface->asyncCall(QStringLiteral("windowUnsnappedForFloat"), windowId);
                        iface->asyncCall(QStringLiteral("setWindowFloating"), windowId, true);
                        iface->asyncCall(QStringLiteral("clearPreTileGeometry"), windowId);
                    }

                    m_effect->emitNavigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"),
                                                     QString(), QString(), screenId);
                });

        return; // Feedback emitted in async callback
    }

    m_effect->emitNavigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"), QString(), QString(),
                                     screenId);
}

void NavigationHandler::executeFloatOff(KWin::EffectWindow* activeWindow, const QString& windowId,
                                        const QString& screenId)
{
    m_floatingWindows.remove(windowId);
    // Also remove appId entry (daemon-sent entries without instance part)
    QString appId = m_effect->extractAppId(windowId);
    if (appId != windowId) {
        m_floatingWindows.remove(appId);
    }

    auto* iface = m_effect->windowTrackingInterface();
    if (iface && iface->isValid()) {
        iface->asyncCall(QStringLiteral("setWindowFloating"), windowId, false);

        QPointer<KWin::EffectWindow> safeWindow = activeWindow;

        QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("calculateUnfloatRestore"), windowId, screenId);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(
            watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, windowId, screenId](QDBusPendingCallWatcher* w) {
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
                bool found = obj.value(QLatin1String("found")).toBool(false);
                if (!found) {
                    qCDebug(lcEffect) << "No pre-float zone found for window";
                    return;
                }

                if (!safeWindow) {
                    qCDebug(lcEffect) << "Window was destroyed during async call";
                    return;
                }

                QStringList zoneIds;
                const QJsonArray zoneArray = obj.value(QLatin1String("zoneIds")).toArray();
                for (const QJsonValue& v : zoneArray) {
                    zoneIds.append(v.toString());
                }

                QRect geometry(obj.value(QLatin1String("x")).toInt(), obj.value(QLatin1String("y")).toInt(),
                               obj.value(QLatin1String("width")).toInt(), obj.value(QLatin1String("height")).toInt());

                QString restoreScreen = obj.value(QLatin1String("screenName")).toString();

                if (!geometry.isValid() || zoneIds.isEmpty()) {
                    qCDebug(lcEffect) << "Invalid geometry or empty zones for unfloat";
                    return;
                }

                // Store floating geometry as pre-snap so next float→unfloat→float works
                auto* iface = m_effect->windowTrackingInterface();
                if (iface && iface->isValid()) {
                    QRectF floatingGeom = safeWindow->frameGeometry();
                    QString floatScreenId = m_effect->getWindowScreenId(safeWindow);
                    qCDebug(lcEffect) << "Storing floating geometry as pre-tile:" << floatingGeom;
                    iface->asyncCall(QStringLiteral("storePreTileGeometry"), windowId,
                                     static_cast<int>(floatingGeom.x()), static_cast<int>(floatingGeom.y()),
                                     static_cast<int>(floatingGeom.width()), static_cast<int>(floatingGeom.height()),
                                     floatScreenId, true);
                }

                qCInfo(lcEffect) << "Applying unfloat geometry:" << geometry << "to zones:" << zoneIds
                                 << "on screen:" << restoreScreen;
                m_effect->applySnapGeometry(safeWindow, geometry);

                if (iface && iface->isValid()) {
                    if (zoneIds.size() > 1) {
                        iface->asyncCall(QStringLiteral("windowSnappedMultiZone"), windowId, zoneIds, restoreScreen);
                    } else {
                        iface->asyncCall(QStringLiteral("windowSnapped"), windowId, zoneIds.first(), restoreScreen);
                    }
                    iface->asyncCall(QStringLiteral("clearPreFloatZone"), windowId);
                }
            });
    }

    m_effect->emitNavigationFeedback(true, QStringLiteral("float"), QStringLiteral("unfloated"), QString(), QString(),
                                     screenId);
}

void NavigationHandler::handleSwapWindows(const QString& targetZoneId, const QString& targetWindowId,
                                          const QString& zoneGeometry)
{
    Q_UNUSED(targetWindowId)
    Q_UNUSED(zoneGeometry)
    qCInfo(lcEffect) << "Swap windows requested (daemon-driven) -" << targetZoneId;

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("swap"));
    if (!activeWindow)
        return;

    QString windowId = m_effect->getWindowId(activeWindow);
    QString screenId = m_effect->getWindowScreenId(activeWindow);

    if (!targetZoneId.startsWith(SwapDirectivePrefix)) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_directive"), QString(),
                                         QString(), screenId);
        return;
    }

    QString direction = targetZoneId.mid(SwapDirectivePrefix.length());

    auto* iface = requireInterface(QStringLiteral("swap"), screenId);
    if (!iface)
        return;

    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    QString capturedWindowId = windowId;
    QString capturedScreenId = screenId;

    QDBusPendingCall pendingCall =
        iface->asyncCall(QStringLiteral("getSwapTargetForWindow"), windowId, direction, screenId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, capturedWindowId, capturedScreenId](QDBusPendingCallWatcher* w) {
                w->deleteLater();

                if (!safeWindow)
                    return;

                QDBusPendingReply<QString> reply = *w;
                if (!reply.isValid()) {
                    m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("dbus_error"),
                                                     QString(), QString(), capturedScreenId);
                    return;
                }

                QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
                if (!doc.isObject())
                    return;

                QJsonObject obj = doc.object();
                if (!obj.value(QLatin1String("success")).toBool(false))
                    return;

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
                QString resultScreenId = obj.value(QLatin1String("screenName")).toString();

                QRect geom1(x1, y1, w1, h1);
                if (!geom1.isValid())
                    return;

                auto* swapIface = m_effect->windowTrackingInterface();
                if (!swapIface || !swapIface->isValid())
                    return;

                if (windowId2.isEmpty()) {
                    m_effect->applySnapGeometry(safeWindow, geom1);
                    swapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, zoneId1, resultScreenId);
                } else {
                    QRect geom2(x2, y2, w2, h2);
                    if (!geom2.isValid())
                        return;

                    KWin::EffectWindow* targetWindow = m_effect->findWindowById(windowId2);

                    if (!targetWindow || !m_effect->shouldHandleWindow(targetWindow)) {
                        m_effect->applySnapGeometry(safeWindow, geom1);
                        swapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, zoneId1,
                                             resultScreenId);
                        return;
                    }

                    m_effect->ensurePreSnapGeometryStored(safeWindow, capturedWindowId);
                    m_effect->ensurePreSnapGeometryStored(targetWindow, windowId2);

                    struct SwapSnap
                    {
                        QPointer<KWin::EffectWindow> window;
                        QRect geometry;
                        QString windowId;
                        QString zoneId;
                    };
                    QVector<SwapSnap> swapEntries = {
                        {safeWindow, geom1, capturedWindowId, zoneId1},
                        {QPointer<KWin::EffectWindow>(targetWindow), geom2, windowId2, zoneId2},
                    };
                    m_effect->applyStaggeredOrImmediate(2, [this, swapEntries, resultScreenId](int i) {
                        const SwapSnap& s = swapEntries[i];
                        if (!s.window) {
                            return;
                        }
                        m_effect->applySnapGeometry(s.window, s.geometry);
                        auto* iface = m_effect->windowTrackingInterface();
                        if (iface && iface->isValid()) {
                            iface->asyncCall(QStringLiteral("windowSnapped"), s.windowId, s.zoneId, resultScreenId);
                        }
                    });
                }
            });
}

NavigationHandler::BatchSnapResult NavigationHandler::applyBatchSnapFromJson(const QString& jsonData,
                                                                             bool filterCurrentDesktop,
                                                                             bool resolveFullWindowId,
                                                                             const std::function<void()>& onComplete)
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

    static constexpr QLatin1String RestoreSentinel{"__restore__"};

    struct PendingSnap
    {
        QPointer<KWin::EffectWindow> window;
        QRect geometry;
        QString snapWindowId;
        QString targetZoneId;
        QString sourceZoneId;
        QString windowScreen;
        bool isRestore = false; // true when targetZoneId == "__restore__"
    };
    QVector<PendingSnap> pending;

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

        // Try exact full windowId match first (current daemon data uses full IDs)
        KWin::EffectWindow* window = windowMap.value(windowId);
        if (!window) {
            // Fall back to appId linear scan for backward compat
            QString appId = m_effect->extractAppId(windowId);
            KWin::EffectWindow* candidate = nullptr;
            int matchCount = 0;
            for (auto it = windowMap.constBegin(); it != windowMap.constEnd(); ++it) {
                if (m_effect->extractAppId(it.key()) == appId) {
                    candidate = it.value();
                    if (++matchCount > 1) {
                        break; // ambiguous — multiple windows share this appId
                    }
                }
            }
            // Only use fallback if exactly one window matched (unambiguous)
            if (matchCount == 1) {
                window = candidate;
            }
        }

        if (!window) {
            continue;
        }
        if (filterCurrentDesktop && (!window->isOnCurrentDesktop() || !window->isOnCurrentActivity())) {
            continue;
        }

        QString snapWindowId = resolveFullWindowId ? m_effect->getWindowId(window) : windowId;
        QString windowScreenId = m_effect->getWindowScreenId(window);

        PendingSnap p;
        p.window = QPointer<KWin::EffectWindow>(window);
        p.geometry = QRect(x, y, width, height);
        p.snapWindowId = snapWindowId;
        p.targetZoneId = targetZoneId;
        p.sourceZoneId = moveObj[QLatin1String("sourceZoneId")].toString();
        p.windowScreen = windowScreenId;
        p.isRestore = (targetZoneId == RestoreSentinel);
        pending.append(p);
    }

    if (pending.isEmpty()) {
        return result;
    }

    // successCount reflects snap entries only (not restore-to-pretile entries).
    // Restore entries are still tracked in snappedWindowIds so autotile's
    // stagger-restore callbacks skip them.
    for (const PendingSnap& p : pending) {
        result.snappedWindowIds.insert(p.snapWindowId);
        if (!p.isRestore) {
            ++result.successCount;
            if (result.firstTargetZoneId.isEmpty()) {
                result.firstSourceZoneId = p.sourceZoneId;
                result.firstTargetZoneId = p.targetZoneId;
                result.firstScreenId = p.windowScreen;
            }
        }
    }
    // Fallback: if ALL entries are restores, use the first entry's screen for feedback
    if (result.firstScreenId.isEmpty()) {
        result.firstScreenId = pending.first().windowScreen;
    }

    // Store pre-snap geometries for ALL windows before any timers fire,
    // so each records its true original geometry (not an intermediate state).
    // Skip restore entries — they're being moved BACK to pre-tile geometry.
    for (const PendingSnap& p : pending) {
        if (p.window && !p.isRestore) {
            m_effect->ensurePreSnapGeometryStored(p.window, p.snapWindowId);
        }
    }

    // Apply geometry per-window via stagger, but defer D-Bus snap confirmations
    // to onComplete so all windows are announced in a single batch call instead
    // of individual D-Bus round-trips per window on the compositor thread.
    m_effect->applyStaggeredOrImmediate(
        pending.size(),
        [this, pending](int i) {
            const PendingSnap& p = pending[i];
            if (!p.window) {
                return;
            }
            m_effect->applySnapGeometry(p.window, p.geometry);
        },
        [this, pending, onComplete]() {
            // Batch all snap confirmations into one D-Bus call
            auto* wiface = m_effect->windowTrackingInterface();
            if (wiface && wiface->isValid()) {
                QJsonArray batchArr;
                for (const PendingSnap& p : pending) {
                    if (!p.window) {
                        continue;
                    }
                    QJsonObject obj;
                    obj[QLatin1String("windowId")] = p.snapWindowId;
                    obj[QLatin1String("zoneId")] = p.targetZoneId;
                    obj[QLatin1String("screenId")] = p.windowScreen;
                    obj[QLatin1String("isRestore")] = p.isRestore;
                    batchArr.append(obj);
                }
                if (!batchArr.isEmpty()) {
                    QString json = QString::fromUtf8(QJsonDocument(batchArr).toJson(QJsonDocument::Compact));
                    wiface->asyncCall(QStringLiteral("windowsSnappedBatch"), json);
                }
            }
            if (onComplete) {
                onComplete();
            }
        });

    return result;
}

void NavigationHandler::handleRotateWindows(bool clockwise, const QString& rotationData)
{
    qCInfo(lcEffect) << "Rotate windows requested, clockwise:" << clockwise;

    KWin::EffectWindow* activeWindow = m_effect->getActiveWindow();
    QString screenId = activeWindow ? m_effect->getWindowScreenId(activeWindow) : QString();

    BatchSnapResult result = applyBatchSnapFromJson(rotationData);

    if (result.status != BatchSnapResult::Success) {
        emitBatchFeedback(result, QStringLiteral("rotate"), screenId);
    } else if (result.successCount > 0) {
        QString direction = clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise");
        QString reason = QStringLiteral("%1:%2").arg(direction).arg(result.successCount);
        m_effect->emitNavigationFeedback(true, QStringLiteral("rotate"), reason, result.firstSourceZoneId,
                                         result.firstTargetZoneId, screenId);
    } else {
        m_effect->emitNavigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_rotations"), QString(),
                                         QString(), screenId);
    }
}

void NavigationHandler::handleResnapToNewLayout(const QString& resnapData)
{
    qCInfo(lcEffect) << "Resnap to new layout requested";

    // Increment generation to invalidate any pending stagger callbacks from
    // previous resnaps. Rapid layout cycling queues multiple stagger timers;
    // only the latest generation's callback should execute.
    const uint64_t gen = ++m_resnapGeneration;

    // Take the saved global stacking order snapshot from slotScreensChanged.
    // This is used to restore z-order after all resnap geometries are applied.
    auto savedStack = m_effect->m_autotileHandler->takeSavedGlobalStack();
    auto* snapAssist = m_effect->m_snapAssistHandler.get();
    auto* autotile = m_effect->m_autotileHandler.get();
    bool snapAssistEnabled = snapAssist && snapAssist->isEnabled();

    std::function<void()> onResnapComplete = [savedStack, snapAssist, autotile, snapAssistEnabled, gen, this]() {
        // Stale callback from a previous resnap — layout has changed since
        if (gen != m_resnapGeneration) {
            return;
        }
        // Restore z-order
        if (!savedStack.isEmpty()) {
            auto* ws = KWin::Workspace::self();
            if (ws) {
                for (const auto& wPtr : savedStack) {
                    if (wPtr && !wPtr->isDeleted()) {
                        KWin::Window* kw = wPtr->window();
                        if (kw) {
                            ws->raiseWindow(kw);
                        }
                    }
                }
            }
        }
        // Show snap assist after all windows are placed
        if (snapAssistEnabled) {
            KWin::EffectWindow* activeWin = m_effect->getActiveWindow();
            QString activeScreenId = activeWin ? m_effect->getWindowScreenId(activeWin) : QString();
            if (!activeScreenId.isEmpty() && !autotile->isAutotileScreen(activeScreenId)) {
                snapAssist->showContinuationIfNeeded(activeScreenId);
            }
        }
    };

    // Apply resnap geometries. The onComplete callback fires AFTER all stagger
    // animations finish, ensuring snap assist sees the final zone occupancy state.
    BatchSnapResult result = applyBatchSnapFromJson(resnapData, /*filterCurrentDesktop=*/true,
                                                    /*resolveFullWindowId=*/true, onResnapComplete);

    const QString screenId = result.firstScreenId;

    if (result.status != BatchSnapResult::Success) {
        emitBatchFeedback(result, QStringLiteral("resnap"), screenId);
    } else if (result.successCount > 0) {
        QString reason = QStringLiteral("resnap:%1").arg(result.successCount);
        m_effect->emitNavigationFeedback(true, QStringLiteral("resnap"), reason, QString(), result.firstTargetZoneId,
                                         screenId);
    } else {
        m_effect->emitNavigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_resnaps"), QString(),
                                         QString(), screenId);
    }
}

void NavigationHandler::handleSnapAllWindows(const QString& snapData, const QString& screenId)
{
    qCDebug(lcEffect) << "Snap all windows handler called for screen:" << screenId;

    BatchSnapResult result = applyBatchSnapFromJson(snapData);

    if (result.status != BatchSnapResult::Success) {
        emitBatchFeedback(result, QStringLiteral("snap_all"), screenId);
    } else if (result.successCount > 0) {
        QString reason = QStringLiteral("snap_all:%1").arg(result.successCount);
        m_effect->emitNavigationFeedback(true, QStringLiteral("snap_all"), reason, QString(), result.firstTargetZoneId,
                                         screenId);
    } else {
        m_effect->emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_snaps"), QString(),
                                         QString(), screenId);
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
    if (!activeWindow)
        return;

    QString windowId = m_effect->getWindowId(activeWindow);
    QString screenId = m_effect->getWindowScreenId(activeWindow);

    auto* iface = requireInterface(QStringLiteral("cycle"), screenId);
    if (!iface)
        return;

    QDBusPendingCall pendingCall =
        iface->asyncCall(QStringLiteral("getCycleTargetForWindow"), windowId, forward, screenId);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QString> reply = *w;
        if (!reply.isValid())
            return;

        QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
        if (!doc.isObject())
            return;

        QJsonObject obj = doc.object();
        if (!obj.value(QLatin1String("success")).toBool(false))
            return;

        QString windowIdToActivate = obj.value(QLatin1String("windowIdToActivate")).toString();
        if (windowIdToActivate.isEmpty())
            return;

        KWin::EffectWindow* win = m_effect->findWindowById(windowIdToActivate);
        if (win) {
            KWin::effects->activateWindow(win);
        }
    });
}

bool NavigationHandler::isWindowFloating(const QString& windowId) const
{
    // Try full window ID first (runtime - distinguishes multiple instances)
    if (m_floatingWindows.contains(windowId)) {
        return true;
    }
    // Fall back to appId (when daemon sends appId without instance part)
    QString appId = m_effect->extractAppId(windowId);
    return (appId != windowId && m_floatingWindows.contains(appId));
}

void NavigationHandler::setWindowFloating(const QString& windowId, bool floating)
{
    if (floating) {
        m_floatingWindows.insert(windowId);
    } else {
        m_floatingWindows.remove(windowId);
        // Also remove appId entry (daemon-sent entries without instance part)
        QString appId = m_effect->extractAppId(windowId);
        if (appId != windowId) {
            m_floatingWindows.remove(appId);
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
