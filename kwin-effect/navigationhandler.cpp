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

    // Check if this is a navigation directive
    if (targetZoneId.startsWith(NavigateDirectivePrefix)) {
        QString direction = targetZoneId.mid(NavigateDirectivePrefix.length());
        qCDebug(lcEffect) << "Navigation direction:" << direction;

        QPointer<KWin::EffectWindow> safeWindow = activeWindow;
        QString capturedWindowId = windowId;
        QString capturedDirection = direction;
        QString capturedScreenName = screenName;
        // Capture pre-snap geometry NOW, before async chain, to avoid race with applySnapGeometry
        QRectF capturedPreSnapGeom = activeWindow->frameGeometry();

        // Step 1: Async query current zone for window
        m_effect->queryZoneForWindowAsync(windowId,
            [this, safeWindow, capturedWindowId, capturedDirection, capturedScreenName, capturedPreSnapGeom](const QString& currentZoneId) {
            if (!safeWindow) return;

            // Shared step: async get geometry and apply snap
            auto applyTargetZone = [this, safeWindow, capturedWindowId, currentZoneId, capturedScreenName](const QString& targetZone) {
                if (!safeWindow || targetZone.isEmpty()) return;

                m_effect->queryZoneGeometryForScreenAsync(targetZone, capturedScreenName,
                    [this, safeWindow, capturedWindowId, currentZoneId, targetZone, capturedScreenName](const QString& geometryJson) {
                        if (!safeWindow) return;
                        QRect geometry = m_effect->parseZoneGeometry(geometryJson);
                        if (!geometry.isValid()) {
                            qCDebug(lcEffect) << "Could not get valid geometry for zone" << targetZone;
                            m_effect->emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("geometry_error"),
                                                             QString(), QString(), capturedScreenName);
                            return;
                        }
                        m_effect->applySnapGeometry(safeWindow, geometry);
                        auto* iface = m_effect->windowTrackingInterface();
                        if (iface && iface->isValid()) {
                            iface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, targetZone, capturedScreenName);
                        }
                        m_effect->emitNavigationFeedback(true, QStringLiteral("move"), QString(), currentZoneId, targetZone, capturedScreenName);
                    });
            };

            // Step 2: Async query target zone
            if (currentZoneId.isEmpty()) {
                qCDebug(lcEffect) << "Window not snapped, finding first zone in direction" << capturedDirection;
                m_effect->ensurePreSnapGeometryStored(safeWindow, capturedWindowId, capturedPreSnapGeom);

                m_effect->queryFirstZoneInDirectionAsync(capturedDirection, capturedScreenName,
                    [this, applyTargetZone, capturedScreenName](const QString& targetZone) {
                    if (targetZone.isEmpty()) {
                        qCDebug(lcEffect) << "No zones available for navigation";
                        m_effect->emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_zones"),
                                                         QString(), QString(), capturedScreenName);
                        return;
                    }
                    applyTargetZone(targetZone);
                });
            } else {
                m_effect->queryAdjacentZoneAsync(currentZoneId, capturedDirection,
                    [this, applyTargetZone, capturedDirection, capturedScreenName](const QString& targetZone) {
                    if (targetZone.isEmpty()) {
                        qCDebug(lcEffect) << "No adjacent zone in direction" << capturedDirection;
                        m_effect->emitNavigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_adjacent_zone"),
                                                         QString(), QString(), capturedScreenName);
                        return;
                    }
                    applyTargetZone(targetZone);
                });
            }
        });

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
        if (!activeWindow) {
            return;
        }

        QString activeWindowId = m_effect->getWindowId(activeWindow);
        QString capturedScreenName = screenName;

        // Step 1: Async query current zone
        m_effect->queryZoneForWindowAsync(activeWindowId,
            [this, capturedScreenName, direction, doFocusInZone](const QString& currentZoneId) {
            if (currentZoneId.isEmpty()) {
                qCDebug(lcEffect) << "Focus navigation requires snapped window";
                m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("not_snapped"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            // Step 2: Async query adjacent zone
            m_effect->queryAdjacentZoneAsync(currentZoneId, direction,
                [this, currentZoneId, capturedScreenName, doFocusInZone](const QString& adjacentZone) {
                if (adjacentZone.isEmpty()) {
                    m_effect->emitNavigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_adjacent_zone"),
                                                     QString(), QString(), capturedScreenName);
                    return;
                }
                doFocusInZone(currentZoneId, adjacentZone, capturedScreenName);
            });
        });
    } else {
        // Direct zone ID - proceed directly to async getWindowsInZone
        doFocusInZone(QString(), targetZoneId, screenName);
    }
}

void NavigationHandler::handleRestoreWindow()
{
    qCInfo(lcEffect) << "Restore window requested";

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
    qCInfo(lcEffect) << "Swap windows requested -" << targetZoneId;

    KWin::EffectWindow* activeWindow = m_effect->getValidActiveWindowOrFail(QStringLiteral("swap"));
    if (!activeWindow) {
        return;
    }

    QString windowId = m_effect->getWindowId(activeWindow);
    QString stableId = m_effect->extractStableId(windowId);
    QString screenName = m_effect->getWindowScreenName(activeWindow);

    // User-initiated snap commands override floating state.
    // windowSnapped() on the daemon will clear floating via clearFloatingStateForSnap().

    if (!targetZoneId.startsWith(SwapDirectivePrefix)) {
        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_directive"),
                                         QString(), QString(), screenName);
        return;
    }

    QString direction = targetZoneId.mid(SwapDirectivePrefix.length());

    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    QString capturedWindowId = windowId;
    QString capturedScreenName = screenName;

    // Step 1: Async query current zone for window
    m_effect->queryZoneForWindowAsync(windowId,
        [this, safeWindow, capturedWindowId, capturedScreenName, direction](const QString& currentZoneId) {
        if (!safeWindow) return;

        if (currentZoneId.isEmpty()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("not_snapped"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        // Step 2: Async query adjacent zone
        m_effect->queryAdjacentZoneAsync(currentZoneId, direction,
            [this, safeWindow, capturedWindowId, capturedScreenName, currentZoneId](const QString& targetZone) {
            if (!safeWindow) return;

            if (targetZone.isEmpty()) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_adjacent_zone"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            auto* iface = m_effect->windowTrackingInterface();
            if (!iface || !iface->isValid()) {
                m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("dbus_error"),
                                                 QString(), QString(), capturedScreenName);
                return;
            }

            // Alias to match existing async chain variable names
            QString capturedCurrentZoneId = currentZoneId;
            QString capturedTargetZone = targetZone;

            // Step 3: Async fetch target zone geometry
            QDBusPendingCall targetGeomCall = iface->asyncCall(QStringLiteral("getZoneGeometryForScreen"), targetZone, capturedScreenName);
            auto* targetGeomWatcher = new QDBusPendingCallWatcher(targetGeomCall, this);

            connect(targetGeomWatcher, &QDBusPendingCallWatcher::finished, this,
                    [this, safeWindow, capturedWindowId, capturedCurrentZoneId, capturedTargetZone, capturedScreenName](QDBusPendingCallWatcher* tgw) {
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

                // Step 4: Async fetch current zone geometry
                auto* iface2 = m_effect->windowTrackingInterface();
                if (!iface2 || !iface2->isValid()) {
                    m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("dbus_error"),
                                                     QString(), QString(), capturedScreenName);
                    return;
                }

                QDBusPendingCall currentGeomCall = iface2->asyncCall(QStringLiteral("getZoneGeometryForScreen"), capturedCurrentZoneId, capturedScreenName);
                auto* currentGeomWatcher = new QDBusPendingCallWatcher(currentGeomCall, this);

                connect(currentGeomWatcher, &QDBusPendingCallWatcher::finished, this,
                        [this, safeWindow, capturedWindowId, capturedCurrentZoneId, capturedTargetZone, capturedScreenName, targetGeom](QDBusPendingCallWatcher* cgw) {
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

                    // Step 5: Async get windows in target zone
                    auto* iface3 = m_effect->windowTrackingInterface();
                    if (!iface3 || !iface3->isValid()) {
                        m_effect->emitNavigationFeedback(false, QStringLiteral("swap"), QStringLiteral("dbus_error"),
                                                         QString(), QString(), capturedScreenName);
                        return;
                    }

                    QDBusPendingCall windowsCall = iface3->asyncCall(QStringLiteral("getWindowsInZone"), capturedTargetZone);
                    auto* windowsWatcher = new QDBusPendingCallWatcher(windowsCall, this);

                    connect(windowsWatcher, &QDBusPendingCallWatcher::finished, this,
                            [this, safeWindow, capturedWindowId, capturedCurrentZoneId, capturedTargetZone, capturedScreenName, targetGeom, currentGeom](QDBusPendingCallWatcher* ww) {
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
                            swapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZone, capturedScreenName);
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
                                swapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZone, capturedScreenName);
                                m_effect->emitNavigationFeedback(true, QStringLiteral("swap"), QStringLiteral("target_not_found"),
                                                                 capturedCurrentZoneId, capturedTargetZone, capturedScreenName);
                                return;
                            }

                            // User-initiated snap commands override floating state.
                            // windowSnapped() on the daemon will clear floating via clearFloatingStateForSnap().

                            m_effect->ensurePreSnapGeometryStored(safeWindow, capturedWindowId);
                            m_effect->ensurePreSnapGeometryStored(targetWindow, targetWindowIdToSwap);

                            m_effect->applySnapGeometry(safeWindow, targetGeom);
                            swapIface->asyncCall(QStringLiteral("windowSnapped"), capturedWindowId, capturedTargetZone, capturedScreenName);

                            m_effect->applySnapGeometry(targetWindow, currentGeom);
                            swapIface->asyncCall(QStringLiteral("windowSnapped"), targetWindowIdToSwap, capturedCurrentZoneId, capturedScreenName);

                            // For swap, highlight both source and target zones
                            m_effect->emitNavigationFeedback(true, QStringLiteral("swap"), QString(),
                                                             capturedCurrentZoneId, capturedTargetZone, capturedScreenName);
                        }
                    });
                });
            });
        });
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
    qCInfo(lcEffect) << "Cycle windows in zone requested -" << directive;

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

    QPointer<KWin::EffectWindow> safeWindow = activeWindow;
    bool capturedForward = forward;
    QString capturedScreenName = screenName;

    // Step 1: Async query current zone for window
    m_effect->queryZoneForWindowAsync(windowId,
        [this, safeWindow, capturedForward, capturedScreenName](const QString& currentZoneId) {
        if (!safeWindow) return;

        if (currentZoneId.isEmpty()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("not_snapped"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        auto* iface = m_effect->windowTrackingInterface();
        if (!iface || !iface->isValid()) {
            m_effect->emitNavigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("dbus_error"),
                                             QString(), QString(), capturedScreenName);
            return;
        }

        QString capturedCurrentZoneId = currentZoneId;

        // Step 2: Async get windows in zone
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
