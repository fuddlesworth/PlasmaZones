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
        // Direct zone ID (e.g., push to empty zone, snap to zone by number)
        // The daemon provides geometry but it may be for the primary screen, not the window's screen.
        // We need screen-specific geometry for multi-monitor support.
        // Use ASYNC D-Bus call to avoid blocking the compositor.

        auto* iface = m_effect->windowTrackingInterface();
        if (!iface || !iface->isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("dbus_error"),
                                             QString(), QString(), screenName);
            return;
        }

        // Capture data for async callback
        // Keep provided geometry as fallback in case async call fails or returns empty
        QPointer<KWin::EffectWindow> safeWindow = activeWindow;
        QString capturedWindowId = windowId;
        QString capturedTargetZoneId = targetZoneId;
        QString capturedScreenName = screenName;
        QString capturedFallbackGeometry = zoneGeometry;

        // BUG FIX: Capture pre-snap geometry NOW, before async call, to avoid race condition
        // Previously, ensurePreSnapGeometryStored was called inside the async callback AFTER
        // applySnapGeometry, which caused it to store the zone geometry instead of the original.
        // By storing the geometry synchronously here, we ensure we capture the correct pre-snap state.
        QRectF preSnapGeom = activeWindow->frameGeometry();
        bool hasValidPreSnapGeom = preSnapGeom.width() > 0 && preSnapGeom.height() > 0;

        // Use async D-Bus call to get screen-specific geometry
        QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("getZoneGeometryForScreen"), targetZoneId, screenName);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeWindow, capturedWindowId, capturedTargetZoneId, capturedScreenName, capturedFallbackGeometry, preSnapGeom, hasValidPreSnapGeom](QDBusPendingCallWatcher* w) {
            w->deleteLater();

            if (!safeWindow) {
                qCDebug(lcEffect) << "Window was destroyed during async call";
                m_effect->emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("window_destroyed"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            QDBusPendingReply<QString> reply = *w;
            QString geometryJson;
            if (reply.isValid() && !reply.value().isEmpty()) {
                geometryJson = reply.value();
            } else if (!capturedFallbackGeometry.isEmpty()) {
                // Use fallback geometry from daemon if async call failed
                geometryJson = capturedFallbackGeometry;
                qCDebug(lcEffect) << "Using fallback geometry from daemon";
            }

            QRect geometry = m_effect->parseZoneGeometry(geometryJson);
            if (!geometry.isValid()) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            // Store pre-snap geometry using the captured value from BEFORE the snap
            // This uses the synchronously captured geometry to avoid race conditions
            if (hasValidPreSnapGeom) {
                // Get fresh interface pointer inside callback - don't use captured pointer
                auto* innerIface = m_effect->windowTrackingInterface();
                if (innerIface && innerIface->isValid()) {
                    // Check if geometry already exists first - only store on FIRST snap
                    QDBusPendingCall hasGeomCall = innerIface->asyncCall(QStringLiteral("hasPreSnapGeometry"), capturedWindowId);
                    auto* hasGeomWatcher = new QDBusPendingCallWatcher(hasGeomCall, this);
                    connect(hasGeomWatcher, &QDBusPendingCallWatcher::finished, this,
                            [this, capturedWindowId, preSnapGeom](QDBusPendingCallWatcher* hgw) {
                        hgw->deleteLater();
                        QDBusPendingReply<bool> hasGeomReply = *hgw;
                        bool hasGeometry = hasGeomReply.isValid() && hasGeomReply.value();
                        if (!hasGeometry) {
                            // Get fresh interface pointer - don't rely on captured pointer
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

            // Get fresh interface pointer inside callback
            auto* snapIface = m_effect->windowTrackingInterface();
            if (snapIface && snapIface->isValid()) {
                snapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZoneId);
            }

            m_effect->emitNavigationFeedback(true, QStringLiteral("push"), QString(), QString(), capturedTargetZoneId, capturedScreenName);
        });
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

    auto* iface = m_effect->windowTrackingInterface();

    // Query daemon's floating state async to ensure local cache is in sync.
    // This fixes race conditions where windowFloatingChanged signal hasn't arrived yet
    // (e.g., after drag-snapping a floating window).
    if (iface && iface->isValid()) {
        QPointer<KWin::EffectWindow> safeWindow = activeWindow;
        QString capturedWindowId = windowId;
        QString capturedStableId = stableId;
        QString capturedScreenName = screenName;

        QDBusPendingCall pendingCall = iface->asyncCall(QStringLiteral("queryWindowFloating"), stableId);
        auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeWindow, capturedWindowId, capturedStableId, capturedScreenName](QDBusPendingCallWatcher* w) {
            w->deleteLater();

            if (!safeWindow) {
                qCDebug(lcEffect) << "Window destroyed during float toggle query";
                return;
            }

            QDBusPendingReply<bool> reply = *w;
            bool daemonFloating = false;
            if (reply.isValid()) {
                daemonFloating = reply.value();
                // Sync local cache with daemon state
                if (daemonFloating != m_floatingWindows.contains(capturedStableId)) {
                    qCDebug(lcEffect) << "Syncing floating state from daemon: stableId=" << capturedStableId
                                      << "local=" << m_floatingWindows.contains(capturedStableId)
                                      << "daemon=" << daemonFloating;
                    setWindowFloating(capturedStableId, daemonFloating);
                }
            }

            // Now perform the toggle with accurate state
            bool newFloatState = !daemonFloating;
            executeFloatToggle(safeWindow, capturedWindowId, capturedStableId, capturedScreenName, newFloatState);
        });
        return; // Actual toggle happens in async callback
    }

    // Fallback if D-Bus not available - use local cache
    bool isCurrentlyFloating = isWindowFloating(stableId);
    bool newFloatState = !isCurrentlyFloating;

    executeFloatToggle(activeWindow, windowId, stableId, screenName, newFloatState);
}

void NavigationHandler::executeFloatToggle(KWin::EffectWindow* activeWindow, const QString& windowId,
                                            const QString& stableId, const QString& screenName, bool newFloatState)
{
    auto* iface = m_effect->windowTrackingInterface();

    if (newFloatState) {
        // Floating ON - restore pre-snap geometry (like drag-unsnap does) and mark as floating
        m_floatingWindows.insert(stableId);

        qCDebug(lcEffect) << "Floating window:" << windowId << "stableId:" << stableId;

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
        m_floatingWindows.remove(stableId);

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

                qCDebug(lcEffect) << "Applying unfloat geometry:" << geometry << "to zones:" << zoneIds;
                m_effect->applySnapGeometry(safeWindow, geometry);

                if (iface && iface->isValid()) {
                    if (zoneIds.size() > 1) {
                        iface->asyncCall(QStringLiteral("windowSnappedMultiZone"), capturedWindowId, zoneIds);
                    } else {
                        iface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, zoneIds.first());
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

    // Capture all needed data for async callback chain
    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    QString capturedWindowId = windowId;
    QString capturedCurrentZoneId = currentZoneId;
    QString capturedTargetZone = targetZone;
    QString capturedStableId = stableId;
    QString capturedScreenName = screenName;

    // Step 1: Async fetch target zone geometry
    QDBusPendingCall targetGeomCall = iface->asyncCall(QStringLiteral("getZoneGeometryForScreen"), targetZone, screenName);
    auto* targetGeomWatcher = new QDBusPendingCallWatcher(targetGeomCall, this);

    connect(targetGeomWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, capturedWindowId, capturedCurrentZoneId, capturedTargetZone, capturedStableId, capturedScreenName](QDBusPendingCallWatcher* tgw) {
        tgw->deleteLater();

        if (!safeWindow) {
            qCDebug(lcEffect) << "Window destroyed during target geometry fetch";
            m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("window_destroyed"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        QDBusPendingReply<QString> targetReply = *tgw;
        if (!targetReply.isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        QRect targetGeom = m_effect->parseZoneGeometry(targetReply.value());
        if (!targetGeom.isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        // Step 2: Async fetch current zone geometry
        auto* iface2 = m_effect->windowTrackingInterface();
        if (!iface2 || !iface2->isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("dbus_error"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        QDBusPendingCall currentGeomCall = iface2->asyncCall(QStringLiteral("getZoneGeometryForScreen"), capturedCurrentZoneId, capturedScreenName);
        auto* currentGeomWatcher = new QDBusPendingCallWatcher(currentGeomCall, this);

        connect(currentGeomWatcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeWindow, capturedWindowId, capturedCurrentZoneId, capturedTargetZone, capturedStableId, capturedScreenName, targetGeom](QDBusPendingCallWatcher* cgw) {
            cgw->deleteLater();

            if (!safeWindow) {
                qCDebug(lcEffect) << "Window destroyed during current geometry fetch";
                m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("window_destroyed"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            QDBusPendingReply<QString> currentReply = *cgw;
            if (!currentReply.isValid()) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            QRect currentGeom = m_effect->parseZoneGeometry(currentReply.value());
            if (!currentGeom.isValid()) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            // Step 3: Async get windows in target zone
            auto* iface3 = m_effect->windowTrackingInterface();
            if (!iface3 || !iface3->isValid()) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("dbus_error"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            QDBusPendingCall windowsCall = iface3->asyncCall(QStringLiteral("getWindowsInZone"), capturedTargetZone);
            auto* windowsWatcher = new QDBusPendingCallWatcher(windowsCall, this);

            connect(windowsWatcher, &QDBusPendingCallWatcher::finished, this,
                    [this, safeWindow, capturedWindowId, capturedCurrentZoneId, capturedTargetZone, capturedStableId, capturedScreenName, targetGeom, currentGeom](QDBusPendingCallWatcher* ww) {
                ww->deleteLater();

                if (!safeWindow) {
                    qCDebug(lcEffect) << "Window destroyed during windows fetch";
                    m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("window_destroyed"),
                                                     QString(), QString(), capturedScreenName);
                    return;
                }

                QDBusPendingReply<QStringList> reply = *ww;
                QStringList windowsInTargetZone;
                if (reply.isValid()) {
                    windowsInTargetZone = reply.value();
                }

                // Get fresh interface pointer - don't use captured pointer from outer scope
                auto* swapIface = m_effect->windowTrackingInterface();
                if (!swapIface || !swapIface->isValid()) {
                    m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("dbus_error"),
                                                     QString(), QString(), capturedScreenName);
                    return;
                }

                if (windowsInTargetZone.isEmpty()) {
                    m_effect->applySnapGeometry(safeWindow, targetGeom);
                    swapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZone);
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
                        swapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZone);
                        m_effect->emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("target_not_found"),
                                                         capturedCurrentZoneId, capturedTargetZone, capturedScreenName);
                        return;
                    }

                    QString targetStableId = m_effect->extractStableId(targetWindowIdToSwap);
                    if (isWindowFloating(targetStableId)) {
                        m_effect->applySnapGeometry(safeWindow, targetGeom);
                        swapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZone);
                        m_effect->emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("target_floating"),
                                                         capturedCurrentZoneId, capturedTargetZone, capturedScreenName);
                        return;
                    }

                    m_effect->ensurePreSnapGeometryStored(safeWindow, capturedWindowId);
                    m_effect->ensurePreSnapGeometryStored(targetWindow, targetWindowIdToSwap);

                    m_effect->applySnapGeometry(safeWindow, targetGeom);
                    swapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZone);

                    m_effect->applySnapGeometry(targetWindow, currentGeom);
                    swapIface->asyncCall(QStringLiteral("windowSnapped"), targetWindowIdToSwap, capturedCurrentZoneId);

                    // For swap, highlight both source and target zones
                    m_effect->emitNavigationFeedback(true, QStringLiteral("swap"), QString(),
                                                     capturedCurrentZoneId, capturedTargetZone, capturedScreenName);
                }
            });
        });
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

void NavigationHandler::handleResnapToNewLayout(const QString& resnapData)
{
    qCDebug(lcEffect) << "Resnap to new layout requested";

    KWin::EffectWindow* activeWindow = m_effect->getActiveWindow();
    QString screenName = activeWindow ? m_effect->getWindowScreenName(activeWindow) : QString();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(resnapData.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("parse_error"),
                                         QString(), QString(), screenName);
        return;
    }

    QJsonArray resnapArray = doc.array();
    if (resnapArray.isEmpty()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_windows"),
                                         QString(), QString(), screenName);
        return;
    }

    auto* iface = m_effect->windowTrackingInterface();
    if (!iface || !iface->isValid()) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("dbus_error"),
                                         QString(), QString(), screenName);
        return;
    }

    QHash<QString, KWin::EffectWindow*> windowMap = m_effect->buildWindowMap();
    int successCount = 0;
    QString firstTargetZoneId;

    for (const QJsonValue& value : resnapArray) {
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

        if (isWindowFloating(stableId)) {
            continue;
        }

        // Only resnap windows on current desktop (and activity) - user expects to see them move
        if (!window->isOnCurrentDesktop() || !window->isOnCurrentActivity()) {
            continue;
        }

        // Use full windowId from KWin for daemon tracking (JSON may contain stableId for pending entries)
        QString fullWindowId = m_effect->getWindowId(window);
        m_effect->ensurePreSnapGeometryStored(window, fullWindowId);
        m_effect->applySnapGeometry(window, QRect(x, y, width, height));
        iface->asyncCall(QStringLiteral("windowSnapped"), fullWindowId, targetZoneId);
        ++successCount;

        if (successCount == 1) {
            firstTargetZoneId = targetZoneId;
        }
    }

    if (successCount > 0) {
        QString reason = QStringLiteral("resnap:%1").arg(successCount);
        m_effect->emitNavigationFeedback(true, QStringLiteral("resnap"), reason,
                                         QString(), firstTargetZoneId, screenName);
    } else {
        m_effect->emitNavigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_resnaps"),
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
