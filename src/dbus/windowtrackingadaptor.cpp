// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingadaptor.h"
#include "windowtrackingadaptor/internal.h"
#include "zonedetectionadaptor.h"
#include "../core/interfaces.h"
#include "../core/layoutmanager.h"
#include "../core/layout.h"
#include "../core/zone.h"
#include "../core/geometryutils.h"
#include "../core/screenmanager.h"
#include "../core/virtualdesktopmanager.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include "../core/types.h"
#include <QGuiApplication>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QTimer>

namespace PlasmaZones {

WindowTrackingAdaptor::WindowTrackingAdaptor(LayoutManager* layoutManager, IZoneDetector* zoneDetector,
                                             ISettings* settings, VirtualDesktopManager* virtualDesktopManager,
                                             QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_layoutManager(layoutManager)
    , m_settings(settings)
    , m_virtualDesktopManager(virtualDesktopManager)
{
    Q_ASSERT(layoutManager);
    Q_ASSERT(zoneDetector);
    Q_ASSERT(settings);

    // Create business logic service
    m_service = new WindowTrackingService(layoutManager, zoneDetector, settings, virtualDesktopManager, this);

    // Forward service signals to D-Bus
    connect(m_service, &WindowTrackingService::windowZoneChanged, this, &WindowTrackingAdaptor::windowZoneChanged);

    // Connect service state changes to persistence
    connect(m_service, &WindowTrackingService::stateChanged, this, &WindowTrackingAdaptor::scheduleSaveState);

    // Setup debounced save timer (500ms delay to batch rapid state changes)
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(500);
    connect(m_saveTimer, &QTimer::timeout, this, &WindowTrackingAdaptor::saveState);

    // Connect to layout changes for pending restores notification
    connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, &WindowTrackingAdaptor::onLayoutChanged);

    // Connect to ScreenManager for panel geometry readiness
    // This is needed to delay window restoration until panel positions are known
    // Use QTimer::singleShot to defer connection until ScreenManager is likely initialized
    QTimer::singleShot(0, this, [this]() {
        if (auto* screenMgr = ScreenManager::instance()) {
            connect(screenMgr, &ScreenManager::panelGeometryReady, this, &WindowTrackingAdaptor::onPanelGeometryReady);
            // If panel geometry is already ready, trigger the check now
            if (ScreenManager::isPanelGeometryReady()) {
                onPanelGeometryReady();
            }
        } else {
            // ScreenManager not available - this is unexpected but we handle it gracefully.
            // Window restoration will still work via onLayoutChanged() -> tryEmitPendingRestoresAvailable()
            // which will emit immediately since isPanelGeometryReady() returns false when instance is null.
            qCWarning(lcDbusWindow)
                << "ScreenManager instance not available - window restoration may use incorrect geometry";
        }
    });

    // Load persisted window tracking state from previous session
    loadState();

    // If we have pending restores but missed activeLayoutChanged (layout was set before we
    // connected), set the flag so tryEmitPendingRestoresAvailable will emit when panel
    // geometry is ready. Fixes daemon restart: windows that were snapped before stop
    // are not re-registered because pendingRestoresAvailable was never emitted.
    if (!m_service->pendingRestoreQueues().isEmpty() && m_layoutManager->activeLayout()) {
        m_hasPendingRestores = true;
        qCDebug(lcDbusWindow) << "Pending restores loaded at init - will emit when panel geometry ready";
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Snapping - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::windowSnapped(const QString& windowId, const QString& zoneId, const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("track window snap"))) {
        return;
    }

    if (zoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot track window snap - empty zone ID";
        return;
    }

    clearFloatingStateForSnap(windowId);

    // Check if this was an auto-snap (restore from session or snap to last zone)
    // and clear the flag. Auto-snapped windows don't update last-used zone tracking.
    bool wasAutoSnapped = m_service->clearAutoSnapped(windowId);

    // If NOT auto-snapped (user explicitly snapped), clear any stale pending
    // assignment from a previous session. This prevents the window from restoring
    // to the wrong zone if it's closed and reopened.
    if (!wasAutoSnapped) {
        m_service->clearStalePendingAssignment(windowId);
    }

    // Use caller-provided screen name if available, otherwise auto-detect,
    // then fall back to cursor/active screen as tertiary fallback
    QString resolvedScreen = resolveScreenForSnap(screenName, zoneId);

    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;

    // Delegate to service
    m_service->assignWindowToZone(windowId, zoneId, resolvedScreen, currentDesktop);

    // Update last used zone (skip zone selector special IDs and auto-snapped windows)
    if (!zoneId.startsWith(QStringLiteral("zoneselector-")) && !wasAutoSnapped) {
        QString windowClass = Utils::extractWindowClass(windowId);
        m_service->updateLastUsedZone(zoneId, resolvedScreen, windowClass, currentDesktop);
    }

    qCInfo(lcDbusWindow) << "Window" << windowId << "snapped to zone" << zoneId << "on screen" << resolvedScreen;
}

void WindowTrackingAdaptor::windowSnappedMultiZone(const QString& windowId, const QStringList& zoneIds,
                                                   const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("track multi-zone window snap"))) {
        return;
    }

    if (zoneIds.isEmpty() || zoneIds.first().isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot track multi-zone window snap - empty zone IDs";
        return;
    }

    clearFloatingStateForSnap(windowId);

    bool wasAutoSnapped = m_service->clearAutoSnapped(windowId);

    if (!wasAutoSnapped) {
        m_service->clearStalePendingAssignment(windowId);
    }

    // Use caller-provided screen name if available, otherwise auto-detect,
    // then fall back to cursor/active screen as tertiary fallback
    QString primaryZoneId = zoneIds.first();
    QString resolvedScreen = resolveScreenForSnap(screenName, primaryZoneId);

    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;

    // Delegate to service with all zone IDs
    m_service->assignWindowToZones(windowId, zoneIds, resolvedScreen, currentDesktop);

    // Update last used zone with primary (skip zone selector special IDs and auto-snapped)
    if (!primaryZoneId.startsWith(QStringLiteral("zoneselector-")) && !wasAutoSnapped) {
        QString windowClass = Utils::extractWindowClass(windowId);
        m_service->updateLastUsedZone(primaryZoneId, resolvedScreen, windowClass, currentDesktop);
    }

    qCInfo(lcDbusWindow) << "Window" << windowId << "snapped to multi-zone:" << zoneIds << "on screen"
                         << resolvedScreen;
}

void WindowTrackingAdaptor::windowUnsnapped(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("untrack window"))) {
        return;
    }

    QString previousZoneId = m_service->zoneForWindow(windowId);
    if (previousZoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Window not found for unsnap:" << windowId;
        return;
    }

    // Clear pending assignment so window won't be auto-restored on next focus/reopen
    m_service->clearStalePendingAssignment(windowId);

    // Delegate to service
    m_service->unassignWindow(windowId);

    qCInfo(lcDbusWindow) << "Window" << windowId << "unsnapped from zone" << previousZoneId;
}

void WindowTrackingAdaptor::setWindowSticky(const QString& windowId, bool sticky)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Delegate to service
    m_service->setWindowSticky(windowId, sticky);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Lifecycle - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::windowClosed(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("clean up closed window"))) {
        return;
    }

    m_service->windowClosed(windowId);
    qCDebug(lcDbusWindow) << "Cleaned up tracking data for closed window" << windowId;
}

void WindowTrackingAdaptor::cursorScreenChanged(const QString& screenName)
{
    if (screenName.isEmpty()) {
        return;
    }
    m_lastCursorScreenName = screenName;
    qCDebug(lcDbusWindow) << "Cursor screen changed to" << screenName;
}

void WindowTrackingAdaptor::windowActivated(const QString& windowId, const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("process windowActivated"))) {
        return;
    }

    // Track the active window's screen as fallback for shortcut screen detection.
    // The primary source is now cursorScreenChanged (from KWin effect's mouseChanged).
    if (!screenName.isEmpty()) {
        m_lastActiveScreenName = screenName;
    }

    qCDebug(lcDbusWindow) << "Window activated:" << windowId << "on screen" << screenName;

    // Update last-used zone when focusing a snapped window
    // Skip auto-snapped windows - only user-focused windows should update the tracking
    QString zoneId = m_service->zoneForWindow(windowId);
    if (!zoneId.isEmpty() && m_settings && m_settings->moveNewWindowsToLastZone()
        && !m_service->isAutoSnapped(windowId)) {
        QString windowClass = Utils::extractWindowClass(windowId);
        int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
        m_service->updateLastUsedZone(zoneId, screenName, windowClass, currentDesktop);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Tracking Queries - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

QString WindowTrackingAdaptor::getZoneForWindow(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("get zone for window"))) {
        return QString();
    }
    // Delegate to service
    return m_service->zoneForWindow(windowId);
}

QStringList WindowTrackingAdaptor::getWindowsInZone(const QString& zoneId)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot get windows in zone - empty zone ID";
        return QStringList();
    }
    // Delegate to service
    return m_service->windowsInZone(zoneId);
}

QStringList WindowTrackingAdaptor::getSnappedWindows()
{
    // Delegate to service
    return m_service->snappedWindows();
}

QString WindowTrackingAdaptor::getEmptyZonesJson(const QString& screenName)
{
    return m_service->getEmptyZonesJson(screenName);
}

QStringList WindowTrackingAdaptor::getMultiZoneForWindow(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("get multi-zone for window"))) {
        return QStringList();
    }

    // Return stored zone IDs directly (multi-zone support)
    return m_service->zonesForWindow(windowId);
}

QString WindowTrackingAdaptor::getLastUsedZoneId()
{
    // Delegate to service
    return m_service->lastUsedZoneId();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Zone Geometry Queries - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

QString WindowTrackingAdaptor::findEmptyZone()
{
    // Use cursor screen for per-screen layout resolution
    return m_service->findEmptyZone(m_lastCursorScreenName);
}

QString WindowTrackingAdaptor::getZoneGeometry(const QString& zoneId)
{
    return getZoneGeometryForScreen(zoneId, QString());
}

QString WindowTrackingAdaptor::getZoneGeometryForScreen(const QString& zoneId, const QString& screenName)
{
    if (zoneId.isEmpty()) {
        qCDebug(lcDbusWindow) << "getZoneGeometryForScreen: empty zone ID";
        return QString();
    }

    // Delegate to service
    QRect geo = m_service->zoneGeometry(zoneId, screenName);
    if (!geo.isValid()) {
        qCDebug(lcDbusWindow) << "getZoneGeometryForScreen: invalid geometry for zone:" << zoneId;
        return QString();
    }

    return rectToJson(geo);
}

} // namespace PlasmaZones
