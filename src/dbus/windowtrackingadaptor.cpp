// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingadaptor.h"
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

static QString serializeRotationEntries(const QVector<RotationEntry>& entries)
{
    if (entries.isEmpty()) {
        return QStringLiteral("[]");
    }
    QJsonArray array;
    for (const RotationEntry& entry : entries) {
        QJsonObject obj;
        obj[QLatin1String("windowId")] = entry.windowId;
        obj[QLatin1String("sourceZoneId")] = entry.sourceZoneId;
        obj[QLatin1String("targetZoneId")] = entry.targetZoneId;
        obj[QLatin1String("x")] = entry.targetGeometry.x();
        obj[QLatin1String("y")] = entry.targetGeometry.y();
        obj[QLatin1String("width")] = entry.targetGeometry.width();
        obj[QLatin1String("height")] = entry.targetGeometry.height();
        array.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

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
        qCInfo(lcDbusWindow) << "calculateUnfloatRestore: saved screen" << restoreScreen
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

void WindowTrackingAdaptor::recordPreAutotileGeometry(const QString& windowId, const QString& screenName,
                                                       int x, int y, int width, int height)
{
    Q_UNUSED(screenName)
    if (windowId.isEmpty() || width <= 0 || height <= 0) {
        return;
    }
    QRect geo(x, y, width, height);
    m_service->storePreAutotileGeometry(windowId, geo);
    qCDebug(lcDbusWindow) << "Recorded pre-autotile geometry for" << windowId << geo;
}

bool WindowTrackingAdaptor::getValidatedPreSnapGeometry(const QString& windowId, int& x, int& y, int& width, int& height)
{
    x = y = width = height = 0;

    if (windowId.isEmpty()) {
        return false;
    }

    // Delegate to service (checks pre-snap first, then pre-autotile for autotile float restore)
    auto geo = m_service->validatedPreSnapOrAutotileGeometry(windowId);
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

void WindowTrackingAdaptor::snapToEmptyZone(const QString& windowId, const QString& windowScreenName, bool sticky,
                                             int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty()) {
        return;
    }

    qCDebug(lcDbusWindow) << "snapToEmptyZone called windowId= " << windowId << "screen= " << windowScreenName;
    SnapResult result = m_service->calculateSnapToEmptyZone(windowId, windowScreenName, sticky);
    if (!result.shouldSnap) {
        qCDebug(lcDbusWindow) << "snapToEmptyZone: no snap";
        return;
    }

    snapX = result.geometry.x();
    snapY = result.geometry.y();
    snapWidth = result.geometry.width();
    snapHeight = result.geometry.height();
    shouldSnap = true;

    clearFloatingStateForSnap(windowId);

    // Mark as auto-snapped so windowSnapped() won't update last-used zone or clear pending
    m_service->markAsAutoSnapped(windowId);

    // Track the assignment
    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    m_service->assignWindowToZone(windowId, result.zoneId, result.screenName, currentDesktop);

    qCInfo(lcDbusWindow) << "Auto-assign snapping window" << windowId << "to empty zone" << result.zoneId;
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
    // Notify effect so it can update its local cache (use full windowId for per-instance tracking)
    Q_EMIT windowFloatingChanged(windowId, floating);
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
    qCInfo(lcDbusWindow) << "moveWindowToAdjacentZone called with direction:" << direction;

    if (!validateDirection(direction, QStringLiteral("move"))) {
        return;
    }

    Q_EMIT moveWindowToZoneRequested(QStringLiteral("navigate:") + direction, QString());
}

void WindowTrackingAdaptor::focusAdjacentZone(const QString& direction)
{
    qCInfo(lcDbusWindow) << "focusAdjacentZone called with direction:" << direction;

    if (!validateDirection(direction, QStringLiteral("focus"))) {
        return;
    }

    Q_EMIT focusWindowInZoneRequested(QStringLiteral("navigate:") + direction, QString());
}

void WindowTrackingAdaptor::pushToEmptyZone(const QString& screenName)
{
    qCInfo(lcDbusWindow) << "pushToEmptyZone called, screen:" << screenName;
    Q_EMIT moveWindowToZoneRequested(QStringLiteral("push"), screenName);
}

void WindowTrackingAdaptor::restoreWindowSize()
{
    qCInfo(lcDbusWindow) << "restoreWindowSize called";
    Q_EMIT restoreWindowRequested();
}

void WindowTrackingAdaptor::toggleWindowFloat()
{
    qCInfo(lcDbusWindow) << "toggleWindowFloat called";
    Q_EMIT toggleWindowFloatRequested(true);
}

void WindowTrackingAdaptor::toggleFloatForWindow(const QString& windowId, const QString& screenName)
{
    qCInfo(lcDbusWindow) << "toggleFloatForWindow called: windowId=" << windowId << "screen=" << screenName;

    if (!validateWindowId(windowId, QStringLiteral("toggle float"))) {
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("invalid_window"),
                                QString(), QString(), screenName);
        return;
    }

    const bool currentlyFloating = m_service->isWindowFloating(windowId);

    if (currentlyFloating) {
        // Unfloat: restore to pre-float zone
        QString restoreJson = calculateUnfloatRestore(windowId, screenName);
        QJsonDocument doc = QJsonDocument::fromJson(restoreJson.toUtf8());
        if (!doc.isObject()) {
            Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_pre_float_zone"),
                                    QString(), QString(), screenName);
            return;
        }
        QJsonObject obj = doc.object();
        if (!obj.value(QLatin1String("found")).toBool(false)) {
            Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_pre_float_zone"),
                                    QString(), QString(), screenName);
            return;
        }
        QStringList zoneIds;
        for (const QJsonValue& v : obj.value(QLatin1String("zoneIds")).toArray()) {
            zoneIds.append(v.toString());
        }
        QRect geo(obj.value(QLatin1String("x")).toInt(), obj.value(QLatin1String("y")).toInt(),
                 obj.value(QLatin1String("width")).toInt(), obj.value(QLatin1String("height")).toInt());
        QString restoreScreen = obj.value(QLatin1String("screenName")).toString();
        if (zoneIds.isEmpty() || !geo.isValid()) {
            Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_pre_float_zone"),
                                    QString(), QString(), screenName);
            return;
        }
        m_service->setWindowFloating(windowId, false);
        m_service->clearPreFloatZone(windowId);

        // Re-assign window to zone(s) directly via service (handles multi-zone correctly).
        // The effect receives applyGeometryRequested with empty zoneId so it only applies geometry
        // without calling windowSnapped (which would lose multi-zone assignment).
        int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
        if (zoneIds.size() > 1) {
            m_service->assignWindowToZones(windowId, zoneIds, restoreScreen, currentDesktop);
        } else {
            m_service->assignWindowToZone(windowId, zoneIds.first(), restoreScreen, currentDesktop);
        }

        Q_EMIT windowFloatingChanged(windowId, false);
        Q_EMIT applyGeometryRequested(windowId, rectToJson(geo), QString(), restoreScreen);
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("unfloated"),
                                QString(), QString(), restoreScreen);
    } else {
        // Float: restore to pre-snap geometry
        int x, y, w, h;
        if (!getValidatedPreSnapGeometry(windowId, x, y, w, h) || w <= 0 || h <= 0) {
            // No pre-snap geometry - still mark as floating, no geometry change
            m_service->unsnapForFloat(windowId);
            m_service->setWindowFloating(windowId, true);
            Q_EMIT windowFloatingChanged(windowId, true);
            Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"),
                                    QString(), QString(), screenName);
            return;
        }
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        m_service->clearPreSnapGeometry(windowId);
        m_service->clearPreAutotileGeometry(windowId);
        QRect geo(x, y, w, h);
        Q_EMIT applyGeometryRequested(windowId, rectToJson(geo), QString(), screenName);
        Q_EMIT windowFloatingChanged(windowId, true);
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"),
                                QString(), QString(), screenName);
    }
}

bool WindowTrackingAdaptor::applyGeometryForFloat(const QString& windowId, const QString& screenName)
{
    int x, y, w, h;
    if (!getValidatedPreSnapGeometry(windowId, x, y, w, h) || w <= 0 || h <= 0) {
        return false;
    }
    m_service->clearPreSnapGeometry(windowId);
    m_service->clearPreAutotileGeometry(windowId);
    QRect geo(x, y, w, h);
    Q_EMIT applyGeometryRequested(windowId, rectToJson(geo), QString(), screenName);
    qCDebug(lcDbusWindow) << "Applied geometry for float:" << windowId << geo;
    return true;
}

void WindowTrackingAdaptor::swapWindowWithAdjacentZone(const QString& direction)
{
    qCInfo(lcDbusWindow) << "swapWindowWithAdjacentZone called with direction:" << direction;

    if (!validateDirection(direction, QStringLiteral("swap"))) {
        return;
    }

    Q_EMIT swapWindowsRequested(QStringLiteral("swap:") + direction, QString(), QString());
}

static QJsonObject moveResult(bool success, const QString& reason, const QString& zoneId,
                              const QString& geometryJson, const QString& sourceZoneId, const QString& screenName)
{
    QJsonObject obj;
    obj[QLatin1String("success")] = success;
    obj[QLatin1String("reason")] = reason;
    obj[QLatin1String("zoneId")] = zoneId;
    obj[QLatin1String("geometryJson")] = geometryJson;
    obj[QLatin1String("sourceZoneId")] = sourceZoneId;
    obj[QLatin1String("screenName")] = screenName;
    return obj;
}

QString WindowTrackingAdaptor::getMoveTargetForWindow(const QString& windowId, const QString& direction,
                                                       const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getMoveTargetForWindow"))) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_window"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }
    if (!validateDirection(direction, QStringLiteral("move"))) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_direction"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }
    if (!m_zoneDetectionAdaptor) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_zone_detection"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    QString targetZoneId;
    if (currentZoneId.isEmpty()) {
        targetZoneId = m_zoneDetectionAdaptor->getFirstZoneInDirection(direction, screenName);
        if (targetZoneId.isEmpty()) {
            Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_zones"),
                                     QString(), QString(), screenName);
            return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_zones"),
                                                            QString(), QString(), QString(), screenName))
                                            .toJson(QJsonDocument::Compact));
        }
    } else {
        targetZoneId = m_zoneDetectionAdaptor->getAdjacentZone(currentZoneId, direction);
        if (targetZoneId.isEmpty()) {
            Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_adjacent_zone"),
                                     currentZoneId, QString(), screenName);
            return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_adjacent_zone"),
                                                            QString(), QString(), currentZoneId, screenName))
                                            .toJson(QJsonDocument::Compact));
        }
    }

    QRect geo = m_service->zoneGeometry(targetZoneId, screenName);
    if (!geo.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("geometry_error"),
                                 currentZoneId, targetZoneId, screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("geometry_error"),
                                                        targetZoneId, QString(), currentZoneId, screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("move"), QString(), currentZoneId, targetZoneId, screenName);
    return QString::fromUtf8(QJsonDocument(moveResult(true, QString(), targetZoneId, rectToJson(geo),
                                                      currentZoneId, screenName))
                                     .toJson(QJsonDocument::Compact));
}

static QJsonObject focusResult(bool success, const QString& reason, const QString& windowIdToActivate,
                               const QString& sourceZoneId, const QString& targetZoneId, const QString& screenName)
{
    QJsonObject obj;
    obj[QLatin1String("success")] = success;
    obj[QLatin1String("reason")] = reason;
    obj[QLatin1String("windowIdToActivate")] = windowIdToActivate;
    obj[QLatin1String("sourceZoneId")] = sourceZoneId;
    obj[QLatin1String("targetZoneId")] = targetZoneId;
    obj[QLatin1String("screenName")] = screenName;
    return obj;
}

QString WindowTrackingAdaptor::getFocusTargetForWindow(const QString& windowId, const QString& direction,
                                                      const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getFocusTargetForWindow"))) {
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("invalid_window"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }
    if (!validateDirection(direction, QStringLiteral("focus"))) {
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("invalid_direction"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }
    if (!m_zoneDetectionAdaptor) {
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("no_zone_detection"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("not_snapped"),
                                 QString(), QString(), screenName);
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("not_snapped"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    QString targetZoneId = m_zoneDetectionAdaptor->getAdjacentZone(currentZoneId, direction);
    if (targetZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_adjacent_zone"),
                                 currentZoneId, QString(), screenName);
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("no_adjacent_zone"),
                                                        QString(), currentZoneId, QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    QStringList windowsInZone = m_service->windowsInZone(targetZoneId);
    if (windowsInZone.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window_in_zone"),
                                 currentZoneId, targetZoneId, screenName);
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("no_window_in_zone"),
                                                        QString(), currentZoneId, targetZoneId, screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("focus"), QString(), currentZoneId, targetZoneId, screenName);
    return QString::fromUtf8(QJsonDocument(focusResult(true, QString(), windowsInZone.first(),
                                                       currentZoneId, targetZoneId, screenName))
                                     .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getRestoreForWindow(const QString& windowId, const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getRestoreForWindow"))) {
        QJsonObject obj;
        obj[QLatin1String("success")] = false;
        obj[QLatin1String("found")] = false;
        obj[QLatin1String("reason")] = QLatin1String("invalid_window");
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

    int x, y, w, h;
    bool found = getValidatedPreSnapGeometry(windowId, x, y, w, h);
    QJsonObject obj;
    obj[QLatin1String("success")] = found && w > 0 && h > 0;
    obj[QLatin1String("found")] = found && w > 0 && h > 0;
    obj[QLatin1String("x")] = x;
    obj[QLatin1String("y")] = y;
    obj[QLatin1String("width")] = w;
    obj[QLatin1String("height")] = h;
    if (!found || w <= 0 || h <= 0) {
        obj[QLatin1String("reason")] = QLatin1String("not_snapped");
        Q_EMIT navigationFeedback(false, QStringLiteral("restore"), QStringLiteral("not_snapped"),
                                 QString(), QString(), screenName);
    } else {
        Q_EMIT navigationFeedback(true, QStringLiteral("restore"), QString(), QString(), QString(), screenName);
    }
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

static QJsonObject cycleResult(bool success, const QString& reason, const QString& windowIdToActivate,
                               const QString& zoneId, const QString& screenName)
{
    QJsonObject obj;
    obj[QLatin1String("success")] = success;
    obj[QLatin1String("reason")] = reason;
    obj[QLatin1String("windowIdToActivate")] = windowIdToActivate;
    obj[QLatin1String("zoneId")] = zoneId;
    obj[QLatin1String("screenName")] = screenName;
    return obj;
}

QString WindowTrackingAdaptor::getCycleTargetForWindow(const QString& windowId, bool forward,
                                                        const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getCycleTargetForWindow"))) {
        return QString::fromUtf8(QJsonDocument(cycleResult(false, QStringLiteral("invalid_window"),
                                                        QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("not_snapped"),
                                 QString(), QString(), screenName);
        return QString::fromUtf8(QJsonDocument(cycleResult(false, QStringLiteral("not_snapped"),
                                                        QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    QStringList windowsInZone = m_service->windowsInZone(currentZoneId);
    if (windowsInZone.size() < 2) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("single_window"),
                                 currentZoneId, currentZoneId, screenName);
        return QString::fromUtf8(QJsonDocument(cycleResult(false, QStringLiteral("single_window"),
                                                        QString(), currentZoneId, screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    int currentIndex = windowsInZone.indexOf(windowId);
    if (currentIndex < 0) {
        currentIndex = 0;
        for (int i = 0; i < windowsInZone.size(); ++i) {
            if (Utils::extractStableId(windowsInZone[i]) == Utils::extractStableId(windowId)) {
                currentIndex = i;
                break;
            }
        }
    }
    int nextIndex = forward ? (currentIndex + 1) % windowsInZone.size()
                            : (currentIndex - 1 + windowsInZone.size()) % windowsInZone.size();
    QString targetWindowId = windowsInZone.at(nextIndex);

    Q_EMIT navigationFeedback(true, QStringLiteral("cycle"), QString(), currentZoneId, currentZoneId, screenName);
    return QString::fromUtf8(QJsonDocument(cycleResult(true, QString(), targetWindowId,
                                                       currentZoneId, screenName))
                                     .toJson(QJsonDocument::Compact));
}

static QJsonObject swapResult(bool success, const QString& reason,
                              const QString& windowId1, int x1, int y1, int w1, int h1, const QString& zoneId1,
                              const QString& windowId2, int x2, int y2, int w2, int h2, const QString& zoneId2,
                              const QString& screenName, const QString& sourceZoneId, const QString& targetZoneId)
{
    QJsonObject obj;
    obj[QLatin1String("success")] = success;
    obj[QLatin1String("reason")] = reason;
    obj[QLatin1String("windowId1")] = windowId1;
    obj[QLatin1String("x1")] = x1;
    obj[QLatin1String("y1")] = y1;
    obj[QLatin1String("w1")] = w1;
    obj[QLatin1String("h1")] = h1;
    obj[QLatin1String("zoneId1")] = zoneId1;
    obj[QLatin1String("windowId2")] = windowId2;
    obj[QLatin1String("x2")] = x2;
    obj[QLatin1String("y2")] = y2;
    obj[QLatin1String("w2")] = w2;
    obj[QLatin1String("h2")] = h2;
    obj[QLatin1String("zoneId2")] = zoneId2;
    obj[QLatin1String("screenName")] = screenName;
    obj[QLatin1String("sourceZoneId")] = sourceZoneId;
    obj[QLatin1String("targetZoneId")] = targetZoneId;
    return obj;
}

QString WindowTrackingAdaptor::getSwapTargetForWindow(const QString& windowId, const QString& direction,
                                                        const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getSwapTargetForWindow"))) {
        return QString::fromUtf8(QJsonDocument(swapResult(false, QStringLiteral("invalid_window"),
                                                        windowId, 0, 0, 0, 0, QString(),
                                                        QString(), 0, 0, 0, 0, QString(),
                                                        screenName, QString(), QString()))
                                        .toJson(QJsonDocument::Compact));
    }
    if (!validateDirection(direction, QStringLiteral("swap"))) {
        return QString::fromUtf8(QJsonDocument(swapResult(false, QStringLiteral("invalid_direction"),
                                                        windowId, 0, 0, 0, 0, QString(),
                                                        QString(), 0, 0, 0, 0, QString(),
                                                        screenName, QString(), QString()))
                                        .toJson(QJsonDocument::Compact));
    }
    if (!m_zoneDetectionAdaptor) {
        return QString::fromUtf8(QJsonDocument(swapResult(false, QStringLiteral("no_zone_detection"),
                                                        windowId, 0, 0, 0, 0, QString(),
                                                        QString(), 0, 0, 0, 0, QString(),
                                                        screenName, QString(), QString()))
                                        .toJson(QJsonDocument::Compact));
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("not_snapped"),
                                 QString(), QString(), screenName);
        return QString::fromUtf8(QJsonDocument(swapResult(false, QStringLiteral("not_snapped"),
                                                        windowId, 0, 0, 0, 0, QString(),
                                                        QString(), 0, 0, 0, 0, QString(),
                                                        screenName, QString(), QString()))
                                        .toJson(QJsonDocument::Compact));
    }

    QString targetZoneId = m_zoneDetectionAdaptor->getAdjacentZone(currentZoneId, direction);
    if (targetZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_adjacent_zone"),
                                 currentZoneId, QString(), screenName);
        return QString::fromUtf8(QJsonDocument(swapResult(false, QStringLiteral("no_adjacent_zone"),
                                                        windowId, 0, 0, 0, 0, QString(),
                                                        QString(), 0, 0, 0, 0, QString(),
                                                        screenName, currentZoneId, QString()))
                                        .toJson(QJsonDocument::Compact));
    }

    QRect targetGeom = m_service->zoneGeometry(targetZoneId, screenName);
    QRect currentGeom = m_service->zoneGeometry(currentZoneId, screenName);
    if (!targetGeom.isValid() || !currentGeom.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"),
                                 currentZoneId, targetZoneId, screenName);
        return QString::fromUtf8(QJsonDocument(swapResult(false, QStringLiteral("geometry_error"),
                                                        windowId, 0, 0, 0, 0, QString(),
                                                        QString(), 0, 0, 0, 0, QString(),
                                                        screenName, currentZoneId, targetZoneId))
                                        .toJson(QJsonDocument::Compact));
    }

    QStringList windowsInTargetZone = m_service->windowsInZone(targetZoneId);
    if (windowsInTargetZone.isEmpty()) {
        Q_EMIT navigationFeedback(true, QStringLiteral("swap"), QStringLiteral("moved_to_empty"),
                                 currentZoneId, targetZoneId, screenName);
        return QString::fromUtf8(QJsonDocument(swapResult(true, QStringLiteral("moved_to_empty"),
                                                        windowId, targetGeom.x(), targetGeom.y(),
                                                        targetGeom.width(), targetGeom.height(), targetZoneId,
                                                        QString(), 0, 0, 0, 0, QString(),
                                                        screenName, currentZoneId, targetZoneId))
                                        .toJson(QJsonDocument::Compact));
    }

    QString targetWindowId = windowsInTargetZone.first();
    Q_EMIT navigationFeedback(true, QStringLiteral("swap"), QString(),
                             currentZoneId, targetZoneId, screenName);
    return QString::fromUtf8(QJsonDocument(swapResult(true, QString(),
                                                      windowId, targetGeom.x(), targetGeom.y(),
                                                      targetGeom.width(), targetGeom.height(), targetZoneId,
                                                      targetWindowId, currentGeom.x(), currentGeom.y(),
                                                      currentGeom.width(), currentGeom.height(), currentZoneId,
                                                      screenName, currentZoneId, targetZoneId))
                                     .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getPushTargetForWindow(const QString& windowId, const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getPushTargetForWindow"))) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_window"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    QString emptyZoneId = m_service->findEmptyZone(screenName);
    if (emptyZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_empty_zone"),
                                 QString(), QString(), screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_empty_zone"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    QRect geo = m_service->zoneGeometry(emptyZoneId, screenName);
    if (!geo.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"),
                                 QString(), emptyZoneId, screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("geometry_error"),
                                                        emptyZoneId, QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("push"), QString(), QString(), emptyZoneId, screenName);
    return QString::fromUtf8(QJsonDocument(moveResult(true, QString(), emptyZoneId, rectToJson(geo),
                                                      QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getSnapToZoneByNumberTarget(const QString& windowId, int zoneNumber,
                                                            const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getSnapToZoneByNumberTarget"))) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_window"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    if (zoneNumber < 1 || zoneNumber > 9) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"),
                                 QString(), QString(), screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_zone_number"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    auto* layout = m_layoutManager->resolveLayoutForScreen(Utils::screenIdForName(screenName));
    if (!layout) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_active_layout"),
                                 QString(), QString(), screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_active_layout"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    Zone* targetZone = nullptr;
    for (Zone* zone : layout->zones()) {
        if (zone->zoneNumber() == zoneNumber) {
            targetZone = zone;
            break;
        }
    }

    if (!targetZone) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("zone_not_found"),
                                 QString(), QString(), screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("zone_not_found"),
                                                        QString(), QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    QString zoneId = targetZone->id().toString();
    QRect geo = m_service->zoneGeometry(zoneId, screenName);
    if (!geo.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("geometry_error"),
                                 QString(), zoneId, screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("geometry_error"),
                                                        zoneId, QString(), QString(), screenName))
                                        .toJson(QJsonDocument::Compact));
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("snap"), QString(), QString(), zoneId, screenName);
    return QString::fromUtf8(QJsonDocument(moveResult(true, QString(), zoneId, rectToJson(geo),
                                                      QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
}

void WindowTrackingAdaptor::snapToZoneByNumber(int zoneNumber, const QString& screenName)
{
    qCInfo(lcDbusWindow) << "snapToZoneByNumber called with zone number:" << zoneNumber
                          << "screen:" << screenName;

    if (zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(lcDbusWindow) << "Invalid zone number:" << zoneNumber << "(must be 1-9)";
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(), QString(), QString());
        return;
    }

    Q_EMIT moveWindowToZoneRequested(QStringLiteral("snap:") + QString::number(zoneNumber), screenName);
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

    QString rotationData = serializeRotationEntries(rotationEntries);
    qCInfo(lcDbusWindow) << "Rotating" << rotationEntries.size() << "windows"
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
    qCInfo(lcDbusWindow) << "cycleWindowsInZone called, forward:" << forward;
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

    QString resnapData = serializeRotationEntries(resnapEntries);
    qCInfo(lcDbusWindow) << "Resnapping" << resnapEntries.size() << "windows to new layout";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

void WindowTrackingAdaptor::resnapCurrentAssignments(const QString& screenFilter)
{
    qCDebug(lcDbusWindow) << "resnapCurrentAssignments called (autotile toggle-off restore)"
                          << "screen:" << (screenFilter.isEmpty() ? QStringLiteral("all") : screenFilter);

    QVector<RotationEntry> entries = m_service->calculateResnapFromCurrentAssignments(screenFilter);
    if (entries.isEmpty()) {
        qCDebug(lcDbusWindow) << "No windows to resnap from current assignments";
        return;
    }

    QString resnapData = serializeRotationEntries(entries);
    qCInfo(lcDbusWindow) << "Resnapping" << entries.size() << "windows to current zone assignments";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

void WindowTrackingAdaptor::snapAllWindows(const QString& screenName)
{
    qCDebug(lcDbusWindow) << "snapAllWindows called for screen:" << screenName;
    Q_EMIT snapAllWindowsRequested(screenName);
}

void WindowTrackingAdaptor::requestMoveSpecificWindowToZone(const QString& windowId, const QString& zoneId,
                                                           const QString& geometryJson)
{
    qCDebug(lcDbusWindow) << "requestMoveSpecificWindowToZone: window=" << windowId << "zone=" << zoneId;
    Q_EMIT moveSpecificWindowToZoneRequested(windowId, zoneId, geometryJson);
}

QString WindowTrackingAdaptor::calculateSnapAllWindows(const QStringList& windowIds, const QString& screenName)
{
    qCDebug(lcDbusWindow) << "calculateSnapAllWindows called with" << windowIds.size()
                          << "windows on screen:" << screenName;

    QVector<RotationEntry> entries = m_service->calculateSnapAllWindows(windowIds, screenName);

    qCInfo(lcDbusWindow) << "Calculated snap-all for" << entries.size() << "windows";
    return serializeRotationEntries(entries);
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

    // Save screen assignments (translate connector names to stable screen IDs for persistence)
    QJsonObject screenAssignmentsObj;
    for (auto it = m_service->screenAssignments().constBegin(); it != m_service->screenAssignments().constEnd(); ++it) {
        QString stableId = Utils::extractStableId(it.key());
        screenAssignmentsObj[stableId] = Utils::screenIdForName(it.value());
    }
    tracking.writeEntry(QStringLiteral("WindowScreenAssignments"),
                        QString::fromUtf8(QJsonDocument(screenAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pending screen assignments (translate to stable screen IDs)
    QJsonObject pendingScreenAssignmentsObj;
    for (auto it = m_service->pendingScreenAssignments().constBegin(); it != m_service->pendingScreenAssignments().constEnd(); ++it) {
        if (!it.value().isEmpty()) {
            pendingScreenAssignmentsObj[it.key()] = Utils::screenIdForName(it.value());
        }
    }
    tracking.writeEntry(QStringLiteral("PendingWindowScreenAssignments"),
                        QString::fromUtf8(QJsonDocument(pendingScreenAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save active desktop assignments (for cross-restart persistence, same pattern as screens)
    QJsonObject desktopAssignmentsObj;
    for (auto it = m_service->desktopAssignments().constBegin(); it != m_service->desktopAssignments().constEnd(); ++it) {
        if (it.value() > 0) {
            QString stableId = Utils::extractStableId(it.key());
            desktopAssignmentsObj[stableId] = it.value();
        }
    }
    tracking.writeEntry(QStringLiteral("WindowDesktopAssignments"),
                        QString::fromUtf8(QJsonDocument(desktopAssignmentsObj).toJson(QJsonDocument::Compact)));

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

    // Save pending zone numbers (for zone-number fallback when UUIDs change)
    QJsonObject pendingZoneNumbersObj;
    for (auto it = m_service->pendingZoneNumbers().constBegin(); it != m_service->pendingZoneNumbers().constEnd(); ++it) {
        QJsonArray numArray;
        for (int num : it.value()) {
            numArray.append(num);
        }
        pendingZoneNumbersObj[it.key()] = numArray;
    }
    tracking.writeEntry(QStringLiteral("PendingWindowZoneNumbers"),
                        QString::fromUtf8(QJsonDocument(pendingZoneNumbersObj).toJson(QJsonDocument::Compact)));

    // Save pre-snap geometries (convert to stableId for cross-restart persistence)
    QJsonObject geometriesObj;
    for (auto it = m_service->preSnapGeometries().constBegin(); it != m_service->preSnapGeometries().constEnd(); ++it) {
        QString key = Utils::extractStableId(it.key());
        QJsonObject geomObj;
        geomObj[QLatin1String("x")] = it.value().x();
        geomObj[QLatin1String("y")] = it.value().y();
        geomObj[QLatin1String("width")] = it.value().width();
        geomObj[QLatin1String("height")] = it.value().height();
        geometriesObj[key] = geomObj;
    }
    tracking.writeEntry(QStringLiteral("PreSnapGeometries"),
                        QString::fromUtf8(QJsonDocument(geometriesObj).toJson(QJsonDocument::Compact)));

    // Save last used zone info (from service)
    tracking.writeEntry(QStringLiteral("LastUsedZoneId"), m_service->lastUsedZoneId());
    // Note: Other last-used fields would need accessors in service

    // Save floating windows (convert to stableId for cross-restart persistence, deduplicate)
    QJsonArray floatingArray;
    QSet<QString> savedFloatingIds;
    for (const QString& windowId : m_service->floatingWindows()) {
        QString stableId = Utils::extractStableId(windowId);
        if (!stableId.isEmpty() && !savedFloatingIds.contains(stableId)) {
            floatingArray.append(stableId);
            savedFloatingIds.insert(stableId);
        }
    }
    tracking.writeEntry(QStringLiteral("FloatingWindows"),
                        QString::fromUtf8(QJsonDocument(floatingArray).toJson(QJsonDocument::Compact)));

    // Save pre-float zone assignments (for unfloating after session restore).
    // Runtime keys may be full window IDs (with pointer address); convert to
    // stable IDs for cross-restart compatibility.
    QJsonObject preFloatZonesObj;
    for (auto it = m_service->preFloatZoneAssignments().constBegin();
         it != m_service->preFloatZoneAssignments().constEnd(); ++it) {
        QString key = Utils::extractStableId(it.key());
        preFloatZonesObj[key] = toJsonArray(it.value());
    }
    tracking.writeEntry(QStringLiteral("PreFloatZoneAssignments"),
                        QString::fromUtf8(QJsonDocument(preFloatZonesObj).toJson(QJsonDocument::Compact)));

    // Save pre-float screen assignments (for unfloating to correct monitor).
    // Same stable ID conversion as above, plus translate to screen IDs.
    QJsonObject preFloatScreensObj;
    for (auto it = m_service->preFloatScreenAssignments().constBegin();
         it != m_service->preFloatScreenAssignments().constEnd(); ++it) {
        QString key = Utils::extractStableId(it.key());
        preFloatScreensObj[key] = Utils::screenIdForName(it.value());
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

void WindowTrackingAdaptor::requestReapplyWindowGeometries()
{
    Q_EMIT reapplyWindowGeometriesRequested();
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

    // Load screen assignments: merge active (WindowScreenAssignments) into pending
    // (PendingWindowScreenAssignments) so that windows that were still open when the
    // daemon last saved state retain their screen assignment after a daemon restart.
    // Without this merge, the screen falls back to wherever KWin initially places the
    // window (typically the primary display), causing the "wrong display" restore bug.
    // This mirrors the zone-assignment merge pattern above.
    QHash<QString, QString> pendingScreens;

    // First: load active screen assignments as a base layer
    // Values may be screen IDs (new) or connector names (legacy) — resolve to current connector name
    QString activeScreensJson = tracking.readEntry(QStringLiteral("WindowScreenAssignments"), QString());
    if (!activeScreensJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(activeScreensJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString() && !it.value().toString().isEmpty()) {
                    QString storedScreen = it.value().toString();
                    // Translate screen ID to connector name; legacy connector names pass through
                    if (!Utils::isConnectorName(storedScreen)) {
                        QString connectorName = Utils::screenNameForId(storedScreen);
                        if (!connectorName.isEmpty()) {
                            storedScreen = connectorName;
                        }
                    }
                    pendingScreens[it.key()] = storedScreen;
                }
            }
        }
    }

    // Second: overlay with pending screen assignments (pending takes priority —
    // these were explicitly saved when the window closed, so they're more recent)
    QString pendingScreensJson = tracking.readEntry(QStringLiteral("PendingWindowScreenAssignments"), QString());
    if (!pendingScreensJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingScreensJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString() && !it.value().toString().isEmpty()) {
                    QString storedScreen = it.value().toString();
                    if (!Utils::isConnectorName(storedScreen)) {
                        QString connectorName = Utils::screenNameForId(storedScreen);
                        if (!connectorName.isEmpty()) {
                            storedScreen = connectorName;
                        }
                    }
                    pendingScreens[it.key()] = storedScreen;
                }
            }
        }
    }
    m_service->setPendingScreenAssignments(pendingScreens);

    // Load desktop assignments: merge active (WindowDesktopAssignments) into pending
    // (PendingWindowDesktopAssignments) so that windows still open at daemon shutdown
    // retain their virtual desktop context. Same merge pattern as screens above.
    QHash<QString, int> pendingDesktops;

    // First: load active desktop assignments as a base layer
    QString activeDesktopsJson = tracking.readEntry(QStringLiteral("WindowDesktopAssignments"), QString());
    if (!activeDesktopsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(activeDesktopsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isDouble() && it.value().toInt() > 0) {
                    pendingDesktops[it.key()] = it.value().toInt();
                }
            }
        }
    }

    // Second: overlay with pending desktop assignments (pending takes priority)
    QString pendingDesktopsJson = tracking.readEntry(QStringLiteral("PendingWindowDesktopAssignments"), QString());
    if (!pendingDesktopsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingDesktopsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isDouble() && it.value().toInt() > 0) {
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

    // Load pending zone numbers (for zone-number fallback when UUIDs change)
    QHash<QString, QList<int>> pendingZoneNumbers;
    QString pendingZoneNumbersJson = tracking.readEntry(QStringLiteral("PendingWindowZoneNumbers"), QString());
    if (!pendingZoneNumbersJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingZoneNumbersJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isArray()) {
                    QList<int> numbers;
                    for (const QJsonValue& v : it.value().toArray()) {
                        if (v.isDouble()) {
                            numbers.append(v.toInt());
                        }
                    }
                    if (!numbers.isEmpty()) {
                        pendingZoneNumbers[it.key()] = numbers;
                    }
                }
            }
        }
    }
    m_service->setPendingZoneNumbers(pendingZoneNumbers);

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
                    QRect geom(geomObj[QLatin1String("x")].toInt(), geomObj[QLatin1String("y")].toInt(),
                               geomObj[QLatin1String("width")].toInt(), geomObj[QLatin1String("height")].toInt());
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
    // Values may be screen IDs (new) or connector names (legacy) — resolve to current connector name
    QHash<QString, QString> preFloatScreens;
    QString preFloatScreensJson = tracking.readEntry(QStringLiteral("PreFloatScreenAssignments"), QString());
    if (!preFloatScreensJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(preFloatScreensJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString()) {
                    QString storedScreen = it.value().toString();
                    if (!Utils::isConnectorName(storedScreen)) {
                        QString connectorName = Utils::screenNameForId(storedScreen);
                        if (!connectorName.isEmpty()) {
                            storedScreen = connectorName;
                        }
                    }
                    preFloatScreens[it.key()] = storedScreen;
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
        Layout* layout = m_layoutManager->layoutForScreen(Utils::screenIdentifier(screen), currentDesktop, m_layoutManager->currentActivity());
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
        Q_EMIT windowFloatingChanged(windowId, false);
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
