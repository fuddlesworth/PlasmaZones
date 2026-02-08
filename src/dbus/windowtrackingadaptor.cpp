// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingadaptor.h"
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

// Static helpers for JSON serialization of zone list maps
static QJsonArray toJsonArray(const QStringList& list)
{
    QJsonArray arr;
    for (const QString& s : list) {
        arr.append(s);
    }
    return arr;
}

static QHash<QString, QStringList> parseZoneListMap(const QString& json)
{
    QHash<QString, QStringList> result;
    if (json.isEmpty()) {
        return result;
    }
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        return result;
    }
    QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.value().isArray()) {
            QStringList zones;
            for (const QJsonValue& v : it.value().toArray()) {
                if (v.isString() && !v.toString().isEmpty()) {
                    zones.append(v.toString());
                }
            }
            if (!zones.isEmpty()) {
                result[it.key()] = zones;
            }
        } else if (it.value().isString() && !it.value().toString().isEmpty()) {
            // Backward compat: old format stored single zone ID string
            result[it.key()] = QStringList{it.value().toString()};
        }
    }
    return result;
}

WindowTrackingAdaptor::WindowTrackingAdaptor(LayoutManager* layoutManager, IZoneDetector* zoneDetector,
                                             ISettings* settings, VirtualDesktopManager* virtualDesktopManager,
                                             QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_layoutManager(layoutManager)
    , m_zoneDetector(zoneDetector)
    , m_settings(settings)
    , m_virtualDesktopManager(virtualDesktopManager)
{
    Q_ASSERT(layoutManager);
    Q_ASSERT(zoneDetector);
    Q_ASSERT(settings);

    // Create business logic service
    m_service = new WindowTrackingService(layoutManager, zoneDetector, settings, virtualDesktopManager, this);

    // Forward service signals to D-Bus
    connect(m_service, &WindowTrackingService::windowZoneChanged,
            this, &WindowTrackingAdaptor::windowZoneChanged);

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
            qCWarning(lcDbusWindow) << "ScreenManager instance not available - window restoration may use incorrect geometry";
        }
    });

    // Load persisted window tracking state from previous session
    loadState();
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

void WindowTrackingAdaptor::windowSnappedMultiZone(const QString& windowId, const QStringList& zoneIds, const QString& screenName)
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

    qCInfo(lcDbusWindow) << "Window" << windowId << "snapped to multi-zone:" << zoneIds << "on screen" << resolvedScreen;
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

void WindowTrackingAdaptor::windowUnsnappedForFloat(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("prepare float"))) {
        return;
    }

    QString previousZoneId = m_service->zoneForWindow(windowId);
    if (previousZoneId.isEmpty()) {
        // Window was not snapped - no-op
        qCDebug(lcDbusWindow) << "windowUnsnappedForFloat: window not in any zone:" << windowId;
        return;
    }

    // Delegate to service
    m_service->unsnapForFloat(windowId);

    qCInfo(lcDbusWindow) << "Window" << windowId << "unsnapped for float from zone" << previousZoneId;
}

bool WindowTrackingAdaptor::getPreFloatZone(const QString& windowId, QString& zoneIdOut)
{
    if (windowId.isEmpty()) {
        qCDebug(lcDbusWindow) << "getPreFloatZone: empty windowId";
        zoneIdOut.clear();
        return false;
    }
    // Delegate to service
    zoneIdOut = m_service->preFloatZone(windowId);
    qCDebug(lcDbusWindow) << "getPreFloatZone for" << windowId << "-> found:" << !zoneIdOut.isEmpty() << "zone:" << zoneIdOut;
    return !zoneIdOut.isEmpty();
}

void WindowTrackingAdaptor::clearPreFloatZone(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Only log if there was something to clear
    bool hadPreFloatZone = !m_service->preFloatZone(windowId).isEmpty();
    // Delegate to service
    m_service->clearPreFloatZone(windowId);
    if (hadPreFloatZone) {
        qCDebug(lcDbusWindow) << "Cleared pre-float zone for window" << windowId;
    }
}

QString WindowTrackingAdaptor::calculateUnfloatRestore(const QString& windowId, const QString& screenName)
{
    QJsonObject result;
    result[QLatin1String("found")] = false;

    if (windowId.isEmpty()) {
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }

    QStringList zoneIds = m_service->preFloatZones(windowId);
    if (zoneIds.isEmpty()) {
        qCDebug(lcDbusWindow) << "calculateUnfloatRestore: no pre-float zones for" << windowId;
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }

    // Use the saved pre-float screen (where the window was snapped before floating)
    // rather than the current window screen, since floating may have moved it cross-monitor.
    // If the saved screen name no longer exists (monitor replugged under a different
    // connector name), fall back to the caller's screen so unfloat still works.
    QString restoreScreen = m_service->preFloatScreen(windowId);
    if (!restoreScreen.isEmpty() && !Utils::findScreenByName(restoreScreen)) {
        qCDebug(lcDbusWindow) << "calculateUnfloatRestore: saved screen" << restoreScreen
                              << "no longer exists, falling back to" << screenName;
        restoreScreen.clear();
    }
    if (restoreScreen.isEmpty()) {
        restoreScreen = screenName;
    }

    // Calculate geometry (combined for multi-zone)
    QRect geo;
    if (zoneIds.size() > 1) {
        geo = m_service->multiZoneGeometry(zoneIds, restoreScreen);
    } else {
        geo = m_service->zoneGeometry(zoneIds.first(), restoreScreen);
    }

    if (!geo.isValid()) {
        qCDebug(lcDbusWindow) << "calculateUnfloatRestore: invalid geometry for zones" << zoneIds;
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }

    result[QLatin1String("found")] = true;
    QJsonArray zoneArray;
    for (const QString& z : zoneIds) {
        zoneArray.append(z);
    }
    result[QLatin1String("zoneIds")] = zoneArray;
    result[QLatin1String("x")] = geo.x();
    result[QLatin1String("y")] = geo.y();
    result[QLatin1String("width")] = geo.width();
    result[QLatin1String("height")] = geo.height();
    result[QLatin1String("screenName")] = restoreScreen;

    qCDebug(lcDbusWindow) << "calculateUnfloatRestore for" << windowId << "-> zones:" << zoneIds << "geo:" << geo;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Pre-Snap Geometry - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::storePreSnapGeometry(const QString& windowId, int x, int y, int width, int height)
{
    if (!validateWindowId(windowId, QStringLiteral("store pre-snap geometry"))) {
        return;
    }

    if (width <= 0 || height <= 0) {
        qCWarning(lcDbusWindow) << "Invalid geometry for pre-snap storage:"
                                << "width=" << width << "height=" << height;
        return;
    }

    // Delegate to service
    m_service->storePreSnapGeometry(windowId, QRect(x, y, width, height));
    qCDebug(lcDbusWindow) << "Stored pre-snap geometry for window" << windowId;
}

bool WindowTrackingAdaptor::getPreSnapGeometry(const QString& windowId, int& x, int& y, int& width, int& height)
{
    x = y = width = height = 0;

    if (!validateWindowId(windowId, QStringLiteral("get pre-snap geometry"))) {
        return false;
    }

    // Delegate to service
    auto geo = m_service->preSnapGeometry(windowId);
    if (!geo) {
        qCDebug(lcDbusWindow) << "No pre-snap geometry stored for window" << windowId;
        return false;
    }

    x = geo->x();
    y = geo->y();
    width = geo->width();
    height = geo->height();
    qCDebug(lcDbusWindow) << "Retrieved pre-snap geometry for window" << windowId << "at" << *geo;
    return true;
}

bool WindowTrackingAdaptor::hasPreSnapGeometry(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    // Delegate to service
    return m_service->hasPreSnapGeometry(windowId);
}

void WindowTrackingAdaptor::clearPreSnapGeometry(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("clear pre-snap geometry"))) {
        return;
    }
    // Only log if there was something to clear
    bool hadGeometry = m_service->hasPreSnapGeometry(windowId);
    // Delegate to service
    m_service->clearPreSnapGeometry(windowId);
    if (hadGeometry) {
        qCDebug(lcDbusWindow) << "Cleared pre-snap geometry for window" << windowId;
    }
}

bool WindowTrackingAdaptor::getValidatedPreSnapGeometry(const QString& windowId, int& x, int& y, int& width, int& height)
{
    x = y = width = height = 0;

    if (windowId.isEmpty()) {
        return false;
    }

    // Delegate to service
    auto geo = m_service->validatedPreSnapGeometry(windowId);
    if (!geo) {
        return false;
    }

    x = geo->x();
    y = geo->y();
    width = geo->width();
    height = geo->height();
    return true;
}

bool WindowTrackingAdaptor::isGeometryOnScreen(int x, int y, int width, int height) const
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    QRect geometry(x, y, width, height);
    for (QScreen* screen : Utils::allScreens()) {
        QRect intersection = screen->geometry().intersected(geometry);
        if (intersection.width() >= MinVisibleWidth && intersection.height() >= MinVisibleHeight) {
            return true;
        }
    }
    return false;
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
// Auto-Snap Operations - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::snapToLastZone(const QString& windowId, const QString& windowScreenName, bool sticky,
                                           int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    // Delegate calculation to service
    SnapResult result = m_service->calculateSnapToLastZone(windowId, windowScreenName, sticky);
    if (!result.shouldSnap) {
        return;
    }

    snapX = result.geometry.x();
    snapY = result.geometry.y();
    snapWidth = result.geometry.width();
    snapHeight = result.geometry.height();
    shouldSnap = true;

    // Mark as auto-snapped so windowSnapped() won't update last-used zone or clear pending
    m_service->markAsAutoSnapped(windowId);

    // Track the assignment
    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    m_service->assignWindowToZone(windowId, result.zoneId, result.screenName, currentDesktop);

    qCInfo(lcDbusWindow) << "Snapping new window" << windowId << "to last used zone" << result.zoneId;
}

void WindowTrackingAdaptor::snapToAppRule(const QString& windowId, const QString& windowScreenName, bool sticky,
                                          int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty()) {
        return;
    }

    // Delegate calculation to service
    SnapResult result = m_service->calculateSnapToAppRule(windowId, windowScreenName, sticky);
    if (!result.shouldSnap) {
        return;
    }

    snapX = result.geometry.x();
    snapY = result.geometry.y();
    snapWidth = result.geometry.width();
    snapHeight = result.geometry.height();
    shouldSnap = true;

    // Mark as auto-snapped so windowSnapped() won't update last-used zone or clear pending
    m_service->markAsAutoSnapped(windowId);

    // Track the assignment
    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    m_service->assignWindowToZone(windowId, result.zoneId, result.screenName, currentDesktop);

    qCInfo(lcDbusWindow) << "App rule snapping window" << windowId << "to zone" << result.zoneId;
}

void WindowTrackingAdaptor::restoreToPersistedZone(const QString& windowId, const QString& screenName, bool sticky,
                                                   int& snapX, int& snapY, int& snapWidth, int& snapHeight,
                                                   bool& shouldRestore)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldRestore = false;

    if (m_settings && !m_settings->restoreWindowsToZonesOnLogin()) {
        qCDebug(lcDbusWindow) << "Session zone restoration disabled by setting";
        return;
    }

    if (windowId.isEmpty()) {
        return;
    }

    // Delegate calculation to service
    SnapResult result = m_service->calculateRestoreFromSession(windowId, screenName, sticky);
    if (!result.shouldSnap) {
        return;
    }

    snapX = result.geometry.x();
    snapY = result.geometry.y();
    snapWidth = result.geometry.width();
    snapHeight = result.geometry.height();
    shouldRestore = true;

    // Mark as auto-snapped so windowSnapped() won't update last-used zone or clear pending
    m_service->markAsAutoSnapped(windowId);

    // Consume the pending assignment so other windows of the same class won't restore to this zone
    m_service->consumePendingAssignment(windowId);

    // Track the assignment (use multi-zone if available)
    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    if (result.zoneIds.size() > 1) {
        m_service->assignWindowToZones(windowId, result.zoneIds, result.screenName, currentDesktop);
    } else {
        m_service->assignWindowToZone(windowId, result.zoneId, result.screenName, currentDesktop);
    }

    qCInfo(lcDbusWindow) << "Restoring window window= " << windowId << " zone(s)= " << result.zoneIds;
}

void WindowTrackingAdaptor::recordSnapIntent(const QString& windowId, bool wasUserInitiated)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Delegate to service
    m_service->recordSnapIntent(windowId, wasUserInitiated);
}

QString WindowTrackingAdaptor::getUpdatedWindowGeometries()
{
    // Delegate to service
    QHash<QString, QRect> geometries = m_service->updatedWindowGeometries();

    if (geometries.isEmpty()) {
        return QStringLiteral("[]");
    }

    QJsonArray windowGeometries;
    for (auto it = geometries.constBegin(); it != geometries.constEnd(); ++it) {
        QJsonObject windowObj;
        windowObj[QLatin1String("windowId")] = it.key();
        windowObj[QLatin1String("x")] = it.value().x();
        windowObj[QLatin1String("y")] = it.value().y();
        windowObj[QLatin1String("width")] = it.value().width();
        windowObj[QLatin1String("height")] = it.value().height();
        windowGeometries.append(windowObj);
    }

    qCDebug(lcDbusWindow) << "Returning updated geometries for" << windowGeometries.size() << "windows";
    return QString::fromUtf8(QJsonDocument(windowGeometries).toJson(QJsonDocument::Compact));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Floating Window Operations - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingAdaptor::isWindowFloating(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    // Delegate to service
    return m_service->isWindowFloating(windowId);
}

bool WindowTrackingAdaptor::queryWindowFloating(const QString& windowId)
{
    return isWindowFloating(windowId);
}

void WindowTrackingAdaptor::setWindowFloating(const QString& windowId, bool floating)
{
    if (!validateWindowId(windowId, QStringLiteral("set float state"))) {
        return;
    }
    // Delegate to service
    m_service->setWindowFloating(windowId, floating);
    qCInfo(lcDbusWindow) << "Window" << windowId << "is now" << (floating ? "floating" : "not floating");
    // Notify effect so it can update its local cache
    Q_EMIT windowFloatingChanged(Utils::extractStableId(windowId), floating);
}

QStringList WindowTrackingAdaptor::getFloatingWindows()
{
    // Delegate to service
    return m_service->floatingWindows();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation Operations - Delegate to Service where possible
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::moveWindowToAdjacentZone(const QString& direction)
{
    qCDebug(lcDbusWindow) << "moveWindowToAdjacentZone called with direction:" << direction;

    if (!validateDirection(direction, QStringLiteral("move"))) {
        return;
    }

    Q_EMIT moveWindowToZoneRequested(QStringLiteral("navigate:") + direction, QString());
}

void WindowTrackingAdaptor::focusAdjacentZone(const QString& direction)
{
    qCDebug(lcDbusWindow) << "focusAdjacentZone called with direction:" << direction;

    if (!validateDirection(direction, QStringLiteral("focus"))) {
        return;
    }

    Q_EMIT focusWindowInZoneRequested(QStringLiteral("navigate:") + direction, QString());
}

void WindowTrackingAdaptor::pushToEmptyZone(const QString& screenName)
{
    qCDebug(lcDbusWindow) << "pushToEmptyZone called, screen:" << screenName;

    // Delegate to service with screen context
    QString emptyZoneId = m_service->findEmptyZone(screenName);
    if (emptyZoneId.isEmpty()) {
        qCDebug(lcDbusWindow) << "No empty zone found";
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_empty_zone"), QString(), QString(), QString());
        return;
    }

    QRect geo = m_service->zoneGeometry(emptyZoneId, screenName);
    if (!geo.isValid()) {
        qCWarning(lcDbusWindow) << "Could not get geometry for empty zone" << emptyZoneId;
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"), QString(), QString(), QString());
        return;
    }

    qCDebug(lcDbusWindow) << "Found empty zone" << emptyZoneId << "with geometry" << geo;
    Q_EMIT moveWindowToZoneRequested(emptyZoneId, rectToJson(geo));
    Q_EMIT navigationFeedback(true, QStringLiteral("push"), QString(), QString(), QString(), QString());
}

void WindowTrackingAdaptor::restoreWindowSize()
{
    qCDebug(lcDbusWindow) << "restoreWindowSize called";
    Q_EMIT restoreWindowRequested();
}

void WindowTrackingAdaptor::toggleWindowFloat()
{
    qCDebug(lcDbusWindow) << "toggleWindowFloat called";
    Q_EMIT toggleWindowFloatRequested(true);
}

void WindowTrackingAdaptor::swapWindowWithAdjacentZone(const QString& direction)
{
    qCDebug(lcDbusWindow) << "swapWindowWithAdjacentZone called with direction:" << direction;

    if (!validateDirection(direction, QStringLiteral("swap"))) {
        return;
    }

    Q_EMIT swapWindowsRequested(QStringLiteral("swap:") + direction, QString(), QString());
}

void WindowTrackingAdaptor::snapToZoneByNumber(int zoneNumber, const QString& screenName)
{
    qCDebug(lcDbusWindow) << "snapToZoneByNumber called with zone number:" << zoneNumber
                          << "screen:" << screenName;

    if (zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(lcDbusWindow) << "Invalid zone number:" << zoneNumber << "(must be 1-9)";
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(), QString(), QString());
        return;
    }

    // Use per-screen layout (falls back to activeLayout via resolveLayoutForScreen)
    auto* layout = m_layoutManager->resolveLayoutForScreen(screenName);
    if (!layout) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_active_layout"), QString(), QString(), QString());
        return;
    }

    Zone* targetZone = nullptr;
    for (Zone* zone : layout->zones()) {
        if (zone->zoneNumber() == zoneNumber) {
            targetZone = zone;
            break;
        }
    }

    if (!targetZone) {
        qCDebug(lcDbusWindow) << "No zone with number" << zoneNumber << "in current layout";
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("zone_not_found"), QString(), QString(), QString());
        return;
    }

    QString zoneId = targetZone->id().toString();
    QRect geo = m_service->zoneGeometry(zoneId, screenName);
    if (!geo.isValid()) {
        qCWarning(lcDbusWindow) << "Could not get geometry for zone" << zoneNumber;
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("geometry_error"), QString(), QString(), QString());
        return;
    }

    qCDebug(lcDbusWindow) << "Snapping to zone" << zoneNumber << "(" << zoneId << ") on screen" << screenName;
    Q_EMIT moveWindowToZoneRequested(zoneId, rectToJson(geo));
    Q_EMIT navigationFeedback(true, QStringLiteral("snap"), QString(), QString(), QString(), QString());
}

void WindowTrackingAdaptor::rotateWindowsInLayout(bool clockwise, const QString& screenName)
{
    qCDebug(lcDbusWindow) << "rotateWindowsInLayout called, clockwise:" << clockwise
                          << "screen:" << screenName;

    // Delegate rotation calculation to service, filtered to cursor screen
    QVector<RotationEntry> rotationEntries = m_service->calculateRotation(clockwise, screenName);

    if (rotationEntries.isEmpty()) {
        auto* layout = getValidatedActiveLayout(QStringLiteral("rotateWindowsInLayout"));
        if (!layout) {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_active_layout"), QString(), QString(), QString());
        } else if (layout->zoneCount() < 2) {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("single_zone"), QString(), QString(), QString());
        } else {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_snapped_windows"), QString(), QString(), QString());
        }
        return;
    }

    QJsonArray rotationArray;
    for (const RotationEntry& entry : rotationEntries) {
        QJsonObject moveObj;
        moveObj[QLatin1String("windowId")] = entry.windowId;
        moveObj[QLatin1String("sourceZoneId")] = entry.sourceZoneId;
        moveObj[QLatin1String("targetZoneId")] = entry.targetZoneId;
        moveObj[QLatin1String("x")] = entry.targetGeometry.x();
        moveObj[QLatin1String("y")] = entry.targetGeometry.y();
        moveObj[QLatin1String("width")] = entry.targetGeometry.width();
        moveObj[QLatin1String("height")] = entry.targetGeometry.height();
        rotationArray.append(moveObj);
    }

    QString rotationData = QString::fromUtf8(QJsonDocument(rotationArray).toJson(QJsonDocument::Compact));
    qCInfo(lcDbusWindow) << "Rotating" << rotationArray.size() << "windows"
                         << (clockwise ? "clockwise" : "counterclockwise");
    Q_EMIT rotateWindowsRequested(clockwise, rotationData);
    // NOTE: Don't emit navigationFeedback here. The KWin effect will report the actual
    // result via reportNavigationFeedback() after performing the rotation, and that
    // feedback will include the zone IDs for proper OSD highlighting. Emitting here
    // would trigger the OSD deduplication logic (same action+reason within 200ms),
    // causing the feedback with zone IDs to be discarded.
}

void WindowTrackingAdaptor::cycleWindowsInZone(bool forward)
{
    qCDebug(lcDbusWindow) << "cycleWindowsInZone called, forward:" << forward;
    QString directive = forward ? QStringLiteral("cycle:forward") : QStringLiteral("cycle:backward");
    Q_EMIT cycleWindowsInZoneRequested(directive, QString());
}

void WindowTrackingAdaptor::resnapToNewLayout()
{
    qCDebug(lcDbusWindow) << "resnapToNewLayout called";

    QVector<RotationEntry> resnapEntries = m_service->calculateResnapFromPreviousLayout();

    if (resnapEntries.isEmpty()) {
        auto* layout = getValidatedActiveLayout(QStringLiteral("resnapToNewLayout"));
        if (!layout) {
            Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_active_layout"),
                                     QString(), QString(), QString());
        } else {
            Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_windows_to_resnap"),
                                     QString(), QString(), QString());
        }
        return;
    }

    QJsonArray resnapArray;
    for (const RotationEntry& entry : resnapEntries) {
        QJsonObject moveObj;
        moveObj[QLatin1String("windowId")] = entry.windowId;
        moveObj[QLatin1String("sourceZoneId")] = entry.sourceZoneId;
        moveObj[QLatin1String("targetZoneId")] = entry.targetZoneId;
        moveObj[QLatin1String("x")] = entry.targetGeometry.x();
        moveObj[QLatin1String("y")] = entry.targetGeometry.y();
        moveObj[QLatin1String("width")] = entry.targetGeometry.width();
        moveObj[QLatin1String("height")] = entry.targetGeometry.height();
        resnapArray.append(moveObj);
    }

    QString resnapData = QString::fromUtf8(QJsonDocument(resnapArray).toJson(QJsonDocument::Compact));
    qCInfo(lcDbusWindow) << "Resnapping" << resnapArray.size() << "windows to new layout";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

void WindowTrackingAdaptor::reportNavigationFeedback(bool success, const QString& action, const QString& reason,
                                                     const QString& sourceZoneId, const QString& targetZoneId,
                                                     const QString& screenName)
{
    qCDebug(lcDbusWindow) << "Navigation feedback: success=" << success << "action=" << action
                          << "reason=" << reason << "sourceZone=" << sourceZoneId << "targetZone=" << targetZoneId
                          << "screen=" << screenName;
    Q_EMIT navigationFeedback(success, action, reason, sourceZoneId, targetZoneId, screenName);
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

// ═══════════════════════════════════════════════════════════════════════════════
// Layout Change Handling
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::onLayoutChanged()
{
    // Delegate to service
    m_service->onLayoutChanged();

    // After layout becomes available, check if we have pending restores
    if (!m_service->pendingZoneAssignments().isEmpty()) {
        m_hasPendingRestores = true;
        qCDebug(lcDbusWindow) << "Layout available with" << m_service->pendingZoneAssignments().size()
                              << "pending restores - checking if panel geometry is ready";
        tryEmitPendingRestoresAvailable();
    }
}

void WindowTrackingAdaptor::onPanelGeometryReady()
{
    qCDebug(lcDbusWindow) << "Panel geometry ready - checking if pending restores available";
    tryEmitPendingRestoresAvailable();
}

void WindowTrackingAdaptor::tryEmitPendingRestoresAvailable()
{
    // Don't emit more than once per session
    if (m_pendingRestoresEmitted) {
        return;
    }

    // Check both conditions: layout has pending restores AND panel geometry is known
    if (!m_hasPendingRestores) {
        qCDebug(lcDbusWindow) << "Cannot emit pendingRestoresAvailable - no pending restores";
        return;
    }

    // Check if panel geometry is ready, or if ScreenManager doesn't exist (fallback)
    // If ScreenManager instance is null, we proceed anyway with a warning - this is
    // better than blocking window restoration indefinitely
    if (ScreenManager::instance() && !ScreenManager::isPanelGeometryReady()) {
        qCDebug(lcDbusWindow) << "Cannot emit pendingRestoresAvailable - panel geometry not ready yet";
        return;
    }

    // Both conditions met (or ScreenManager unavailable) - emit the signal
    m_pendingRestoresEmitted = true;
    if (!ScreenManager::instance()) {
        qCWarning(lcDbusWindow) << "Emitting pendingRestoresAvailable without ScreenManager - geometry may be incorrect";
    } else {
        qCInfo(lcDbusWindow) << "Panel geometry ready AND pending restores available - notifying effect";
    }
    Q_EMIT pendingRestoresAvailable();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Persistence (Adaptor Responsibility: KConfig)
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::saveState()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup tracking = config->group(QStringLiteral("WindowTracking"));

    // Save zone assignments as JSON arrays (from service state)
    QJsonObject assignmentsObj;
    for (auto it = m_service->zoneAssignments().constBegin(); it != m_service->zoneAssignments().constEnd(); ++it) {
        QString stableId = Utils::extractStableId(it.key());
        assignmentsObj[stableId] = toJsonArray(it.value());
    }
    // Include pending assignments
    for (auto it = m_service->pendingZoneAssignments().constBegin(); it != m_service->pendingZoneAssignments().constEnd(); ++it) {
        if (!assignmentsObj.contains(it.key())) {
            assignmentsObj[it.key()] = toJsonArray(it.value());
        }
    }
    tracking.writeEntry(QStringLiteral("WindowZoneAssignments"),
                        QString::fromUtf8(QJsonDocument(assignmentsObj).toJson(QJsonDocument::Compact)));

    // Save screen assignments
    QJsonObject screenAssignmentsObj;
    for (auto it = m_service->screenAssignments().constBegin(); it != m_service->screenAssignments().constEnd(); ++it) {
        QString stableId = Utils::extractStableId(it.key());
        screenAssignmentsObj[stableId] = it.value();
    }
    tracking.writeEntry(QStringLiteral("WindowScreenAssignments"),
                        QString::fromUtf8(QJsonDocument(screenAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pending screen assignments
    QJsonObject pendingScreenAssignmentsObj;
    for (auto it = m_service->pendingScreenAssignments().constBegin(); it != m_service->pendingScreenAssignments().constEnd(); ++it) {
        if (!it.value().isEmpty()) {
            pendingScreenAssignmentsObj[it.key()] = it.value();
        }
    }
    tracking.writeEntry(QStringLiteral("PendingWindowScreenAssignments"),
                        QString::fromUtf8(QJsonDocument(pendingScreenAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pending desktop assignments
    QJsonObject pendingDesktopAssignmentsObj;
    for (auto it = m_service->pendingDesktopAssignments().constBegin(); it != m_service->pendingDesktopAssignments().constEnd(); ++it) {
        if (it.value() > 0) {
            pendingDesktopAssignmentsObj[it.key()] = it.value();
        }
    }
    tracking.writeEntry(QStringLiteral("PendingWindowDesktopAssignments"),
                        QString::fromUtf8(QJsonDocument(pendingDesktopAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pending layout assignments (for layout validation on restore)
    QJsonObject pendingLayoutAssignmentsObj;
    for (auto it = m_service->pendingLayoutAssignments().constBegin(); it != m_service->pendingLayoutAssignments().constEnd(); ++it) {
        if (!it.value().isEmpty()) {
            pendingLayoutAssignmentsObj[it.key()] = it.value();
        }
    }
    tracking.writeEntry(QStringLiteral("PendingWindowLayoutAssignments"),
                        QString::fromUtf8(QJsonDocument(pendingLayoutAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pre-snap geometries
    QJsonObject geometriesObj;
    for (auto it = m_service->preSnapGeometries().constBegin(); it != m_service->preSnapGeometries().constEnd(); ++it) {
        QJsonObject geomObj;
        geomObj[QStringLiteral("x")] = it.value().x();
        geomObj[QStringLiteral("y")] = it.value().y();
        geomObj[QStringLiteral("width")] = it.value().width();
        geomObj[QStringLiteral("height")] = it.value().height();
        geometriesObj[it.key()] = geomObj;
    }
    tracking.writeEntry(QStringLiteral("PreSnapGeometries"),
                        QString::fromUtf8(QJsonDocument(geometriesObj).toJson(QJsonDocument::Compact)));

    // Save last used zone info (from service)
    tracking.writeEntry(QStringLiteral("LastUsedZoneId"), m_service->lastUsedZoneId());
    // Note: Other last-used fields would need accessors in service

    // Save floating windows (already stored as stableIds in service)
    QJsonArray floatingArray;
    for (const QString& stableId : m_service->floatingWindows()) {
        floatingArray.append(stableId);
    }
    tracking.writeEntry(QStringLiteral("FloatingWindows"),
                        QString::fromUtf8(QJsonDocument(floatingArray).toJson(QJsonDocument::Compact)));

    // Save pre-float zone assignments (for unfloating after session restore)
    QJsonObject preFloatZonesObj;
    for (auto it = m_service->preFloatZoneAssignments().constBegin();
         it != m_service->preFloatZoneAssignments().constEnd(); ++it) {
        preFloatZonesObj[it.key()] = toJsonArray(it.value());
    }
    tracking.writeEntry(QStringLiteral("PreFloatZoneAssignments"),
                        QString::fromUtf8(QJsonDocument(preFloatZonesObj).toJson(QJsonDocument::Compact)));

    // Save pre-float screen assignments (for unfloating to correct monitor)
    QJsonObject preFloatScreensObj;
    for (auto it = m_service->preFloatScreenAssignments().constBegin();
         it != m_service->preFloatScreenAssignments().constEnd(); ++it) {
        preFloatScreensObj[it.key()] = it.value();
    }
    tracking.writeEntry(QStringLiteral("PreFloatScreenAssignments"),
                        QString::fromUtf8(QJsonDocument(preFloatScreensObj).toJson(QJsonDocument::Compact)));

    // Save user-snapped classes
    QJsonArray userSnappedArray;
    for (const QString& windowClass : m_service->userSnappedClasses()) {
        userSnappedArray.append(windowClass);
    }
    tracking.writeEntry(QStringLiteral("UserSnappedClasses"),
                        QString::fromUtf8(QJsonDocument(userSnappedArray).toJson(QJsonDocument::Compact)));

    config->sync();
    qCInfo(lcDbusWindow) << "Saved state to KConfig";
}

void WindowTrackingAdaptor::loadState()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup tracking = config->group(QStringLiteral("WindowTracking"));

    // Load zone assignments into pending (keyed by stable ID)
    // Supports both old format (string) and new format (JSON array) for backward compat
    QHash<QString, QStringList> pendingZones = parseZoneListMap(
        tracking.readEntry(QStringLiteral("WindowZoneAssignments"), QString()));
    m_service->setPendingZoneAssignments(pendingZones);

    // Load pending screen assignments
    QHash<QString, QString> pendingScreens;
    QString pendingScreensJson = tracking.readEntry(QStringLiteral("PendingWindowScreenAssignments"), QString());
    if (!pendingScreensJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingScreensJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString()) {
                    pendingScreens[it.key()] = it.value().toString();
                }
            }
        }
    }
    m_service->setPendingScreenAssignments(pendingScreens);

    // Load pending desktop assignments
    QHash<QString, int> pendingDesktops;
    QString pendingDesktopsJson = tracking.readEntry(QStringLiteral("PendingWindowDesktopAssignments"), QString());
    if (!pendingDesktopsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingDesktopsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isDouble()) {
                    pendingDesktops[it.key()] = it.value().toInt();
                }
            }
        }
    }
    m_service->setPendingDesktopAssignments(pendingDesktops);

    // Load pending layout assignments (for layout validation on restore)
    QHash<QString, QString> pendingLayouts;
    QString pendingLayoutsJson = tracking.readEntry(QStringLiteral("PendingWindowLayoutAssignments"), QString());
    if (!pendingLayoutsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingLayoutsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString()) {
                    pendingLayouts[it.key()] = it.value().toString();
                }
            }
        }
    }
    m_service->setPendingLayoutAssignments(pendingLayouts);

    // Load pre-snap geometries
    QHash<QString, QRect> preSnapGeometries;
    QString geometriesJson = tracking.readEntry(QStringLiteral("PreSnapGeometries"), QString());
    if (!geometriesJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(geometriesJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isObject()) {
                    QJsonObject geomObj = it.value().toObject();
                    QRect geom(geomObj[QStringLiteral("x")].toInt(), geomObj[QStringLiteral("y")].toInt(),
                               geomObj[QStringLiteral("width")].toInt(), geomObj[QStringLiteral("height")].toInt());
                    if (geom.width() > 0 && geom.height() > 0) {
                        preSnapGeometries[it.key()] = geom;
                    }
                }
            }
        }
    }
    m_service->setPreSnapGeometries(preSnapGeometries);

    // Load last used zone info
    QString lastZoneId = tracking.readEntry(QStringLiteral("LastUsedZoneId"), QString());
    QString lastScreenName = tracking.readEntry(QStringLiteral("LastUsedScreenName"), QString());
    QString lastZoneClass = tracking.readEntry(QStringLiteral("LastUsedZoneClass"), QString());
    int lastDesktop = tracking.readEntry(QStringLiteral("LastUsedDesktop"), 0);
    m_service->setLastUsedZone(lastZoneId, lastScreenName, lastZoneClass, lastDesktop);

    // Load floating windows
    QSet<QString> floatingWindows;
    QString floatingJson = tracking.readEntry(QStringLiteral("FloatingWindows"), QString());
    if (!floatingJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(floatingJson.toUtf8());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                if (val.isString()) {
                    floatingWindows.insert(val.toString());
                }
            }
        }
    }
    m_service->setFloatingWindows(floatingWindows);

    // Load pre-float zone assignments (for unfloating after session restore)
    // Supports both old format (string) and new format (JSON array) for backward compat
    QHash<QString, QStringList> preFloatZones = parseZoneListMap(
        tracking.readEntry(QStringLiteral("PreFloatZoneAssignments"), QString()));
    m_service->setPreFloatZoneAssignments(preFloatZones);

    // Load pre-float screen assignments (for unfloating to correct monitor)
    QHash<QString, QString> preFloatScreens;
    QString preFloatScreensJson = tracking.readEntry(QStringLiteral("PreFloatScreenAssignments"), QString());
    if (!preFloatScreensJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(preFloatScreensJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString()) {
                    preFloatScreens[it.key()] = it.value().toString();
                }
            }
        }
    }
    m_service->setPreFloatScreenAssignments(preFloatScreens);

    // Load user-snapped classes
    QSet<QString> userSnappedClasses;
    QString userSnappedJson = tracking.readEntry(QStringLiteral("UserSnappedClasses"), QString());
    if (!userSnappedJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(userSnappedJson.toUtf8());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                if (val.isString()) {
                    userSnappedClasses.insert(val.toString());
                }
            }
        }
    }
    m_service->setUserSnappedClasses(userSnappedClasses);

    qCInfo(lcDbusWindow) << "Loaded state from KConfig pendingAssignments= " << pendingZones.size();
    for (auto it = pendingZones.constBegin(); it != pendingZones.constEnd(); ++it) {
        qCInfo(lcDbusWindow) << "  Pending snap window= " << it.key() << " zone= " << it.value();
    }
    if (!pendingZones.isEmpty()) {
        m_hasPendingRestores = true;
        tryEmitPendingRestoresAvailable();
    }
}

void WindowTrackingAdaptor::scheduleSaveState()
{
    if (m_saveTimer) {
        m_saveTimer->start();
    } else {
        saveState();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Methods
// ═══════════════════════════════════════════════════════════════════════════════

Layout* WindowTrackingAdaptor::getValidatedActiveLayout(const QString& operation) const
{
    if (!m_layoutManager) {
        qCWarning(lcDbusWindow) << "No layout manager for" << operation;
        return nullptr;
    }

    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        qCDebug(lcDbusWindow) << "No active layout for" << operation;
        return nullptr;
    }

    return layout;
}

bool WindowTrackingAdaptor::validateWindowId(const QString& windowId, const QString& operation) const
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot" << operation << "- empty window ID";
        return false;
    }
    return true;
}

bool WindowTrackingAdaptor::validateDirection(const QString& direction, const QString& action)
{
    if (direction.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot" << action << "- empty direction";
        Q_EMIT navigationFeedback(false, action, QStringLiteral("invalid_direction"), QString(), QString(), QString());
        return false;
    }
    return true;
}

QString WindowTrackingAdaptor::detectScreenForZone(const QString& zoneId) const
{
    if (!m_layoutManager) {
        return QString();
    }
    auto zoneUuid = Utils::parseUuid(zoneId);
    if (!zoneUuid) {
        return QString();
    }

    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;

    // Search per-screen layouts to find which screen's layout contains this zone.
    // This correctly handles multi-monitor setups where each screen has a different layout.
    for (QScreen* screen : Utils::allScreens()) {
        Layout* layout = m_layoutManager->layoutForScreen(screen->name(), currentDesktop, m_layoutManager->currentActivity());
        if (layout && layout->zoneById(*zoneUuid)) {
            return screen->name();
        }
    }

    // Fallback: zone not in any screen-specific layout, try geometry projection
    // with the active layout (single-monitor or unconfigured multi-monitor)
    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        return QString();
    }
    Zone* zone = layout->zoneById(*zoneUuid);
    if (!zone) {
        return QString();
    }
    QRectF relGeom = zone->relativeGeometry();
    for (QScreen* screen : Utils::allScreens()) {
        QRect availGeom = ScreenManager::actualAvailableGeometry(screen);
        QPoint zoneCenter(availGeom.x() + static_cast<int>(relGeom.center().x() * availGeom.width()),
                          availGeom.y() + static_cast<int>(relGeom.center().y() * availGeom.height()));
        if (screen->geometry().contains(zoneCenter)) {
            return screen->name();
        }
    }
    return QString();
}

QString WindowTrackingAdaptor::resolveScreenForSnap(const QString& callerScreen, const QString& zoneId) const
{
    if (!callerScreen.isEmpty()) {
        return callerScreen;
    }
    QString detected = detectScreenForZone(zoneId);
    if (!detected.isEmpty()) {
        return detected;
    }
    // Tertiary: use cursor or active window screen
    if (!m_lastCursorScreenName.isEmpty()) {
        return m_lastCursorScreenName;
    }
    return m_lastActiveScreenName;
}

void WindowTrackingAdaptor::clearFloatingStateForSnap(const QString& windowId)
{
    if (m_service->isWindowFloating(windowId)) {
        qCDebug(lcDbusWindow) << "Window" << windowId << "was floating, clearing floating state for snap";
        m_service->setWindowFloating(windowId, false);
        m_service->clearPreFloatZone(windowId);
        Q_EMIT windowFloatingChanged(Utils::extractStableId(windowId), false);
    }
}

QString WindowTrackingAdaptor::rectToJson(const QRect& rect) const
{
    QJsonObject geomObj;
    geomObj[QLatin1String("x")] = rect.x();
    geomObj[QLatin1String("y")] = rect.y();
    geomObj[QLatin1String("width")] = rect.width();
    geomObj[QLatin1String("height")] = rect.height();
    return QString::fromUtf8(QJsonDocument(geomObj).toJson(QJsonDocument::Compact));
}

} // namespace PlasmaZones
