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

    // Load persisted window tracking state from previous session
    loadState();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Snapping - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::windowSnapped(const QString& windowId, const QString& zoneId)
{
    if (!validateWindowId(windowId, QStringLiteral("track window snap"))) {
        return;
    }

    if (zoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot track window snap - empty zone ID";
        return;
    }

    // Determine screen for this zone
    QString screenName;
    if (m_layoutManager) {
        auto* layout = m_layoutManager->activeLayout();
        if (layout) {
            auto zoneUuid = Utils::parseUuid(zoneId);
            Zone* zone = zoneUuid ? layout->zoneById(*zoneUuid) : nullptr;
            if (zone) {
                QRectF relGeom = zone->relativeGeometry();
                for (QScreen* screen : Utils::allScreens()) {
                    QRect availGeom = ScreenManager::actualAvailableGeometry(screen);
                    QPoint zoneCenter(availGeom.x() + static_cast<int>(relGeom.center().x() * availGeom.width()),
                                      availGeom.y() + static_cast<int>(relGeom.center().y() * availGeom.height()));
                    if (screen->geometry().contains(zoneCenter)) {
                        screenName = screen->name();
                        break;
                    }
                }
            }
        }
    }

    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;

    // Delegate to service
    m_service->assignWindowToZone(windowId, zoneId, screenName, currentDesktop);

    // Update last used zone (skip zone selector special IDs and auto-snapped windows)
    if (!zoneId.startsWith(QStringLiteral("zoneselector-"))) {
        QString windowClass = Utils::extractWindowClass(windowId);
        m_service->updateLastUsedZone(zoneId, screenName, windowClass, currentDesktop);
    }

    qCDebug(lcDbusWindow) << "Window" << windowId << "snapped to zone" << zoneId;
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

    // Delegate to service
    m_service->unassignWindow(windowId);

    qCDebug(lcDbusWindow) << "Window" << windowId << "unsnapped from zone" << previousZoneId;
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
        return;
    }

    // Delegate to service
    m_service->unsnapForFloat(windowId);

    qCDebug(lcDbusWindow) << "Window" << windowId << "unsnapped for float from zone" << previousZoneId;
}

bool WindowTrackingAdaptor::getPreFloatZone(const QString& windowId, QString& zoneIdOut)
{
    if (windowId.isEmpty()) {
        zoneIdOut.clear();
        return false;
    }
    // Delegate to service
    zoneIdOut = m_service->preFloatZone(windowId);
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

    // Delegate to service
    m_service->windowClosed(windowId);
    qCDebug(lcDbusWindow) << "Cleaned up tracking data for closed window" << windowId;

    // Emit window removed event for autotiling consumers
    Q_EMIT windowRemovedEvent(windowId);
}

void WindowTrackingAdaptor::windowAdded(const QString& windowId, const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("process windowAdded"))) {
        return;
    }

    qCDebug(lcDbusWindow) << "Window added:" << windowId << "on screen" << screenName;
    Q_EMIT windowAddedEvent(windowId, screenName);
}

void WindowTrackingAdaptor::windowActivated(const QString& windowId, const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("process windowActivated"))) {
        return;
    }

    qCDebug(lcDbusWindow) << "Window activated:" << windowId << "on screen" << screenName;

    // Update last-used zone when focusing a snapped window
    QString zoneId = m_service->zoneForWindow(windowId);
    if (!zoneId.isEmpty() && m_settings && m_settings->moveNewWindowsToLastZone()) {
        QString windowClass = Utils::extractWindowClass(windowId);
        int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
        m_service->updateLastUsedZone(zoneId, screenName, windowClass, currentDesktop);
    }

    Q_EMIT windowActivatedEvent(windowId, screenName);
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
    QStringList zoneIds;

    if (!validateWindowId(windowId, QStringLiteral("get multi-zone for window"))) {
        return zoneIds;
    }

    QString primaryZoneId = m_service->zoneForWindow(windowId);
    if (primaryZoneId.isEmpty()) {
        return zoneIds;
    }

    auto* layout = getValidatedActiveLayout(QStringLiteral("getMultiZoneForWindow"));
    if (!layout) {
        return zoneIds;
    }

    auto primaryUuid = Utils::parseUuid(primaryZoneId);
    if (!primaryUuid) {
        return zoneIds;
    }

    Zone* primaryZone = layout->zoneById(*primaryUuid);
    if (!primaryZone) {
        return zoneIds;
    }

    zoneIds.append(primaryZoneId);

    if (m_zoneDetector) {
        m_zoneDetector->setLayout(layout);
        QRectF primaryGeom = primaryZone->geometry();
        QPointF centerPoint(primaryGeom.center().x(), primaryGeom.center().y());
        QVector<Zone*> nearbyZones = m_zoneDetector->zonesNearEdge(centerPoint);

        for (Zone* zone : nearbyZones) {
            if (zone && zone != primaryZone) {
                const QRectF& r1 = primaryZone->geometry();
                const QRectF& r2 = zone->geometry();
                const qreal threshold = 20.0;

                bool isAdjacent = false;
                if ((qAbs(r1.right() - r2.left()) <= threshold || qAbs(r2.right() - r1.left()) <= threshold)) {
                    if (r1.top() < r2.bottom() && r1.bottom() > r2.top()) {
                        isAdjacent = true;
                    }
                }
                if (!isAdjacent && (qAbs(r1.bottom() - r2.top()) <= threshold || qAbs(r2.bottom() - r1.top()) <= threshold)) {
                    if (r1.left() < r2.right() && r1.right() > r2.left()) {
                        isAdjacent = true;
                    }
                }

                if (isAdjacent) {
                    zoneIds.append(zone->id().toString());
                }
            }
        }
    }

    return zoneIds;
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

    // Track the assignment
    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    m_service->assignWindowToZone(windowId, result.zoneId, result.screenName, currentDesktop);

    qCDebug(lcDbusWindow) << "Snapping new window" << windowId << "to last used zone" << result.zoneId;
}

void WindowTrackingAdaptor::restoreToPersistedZone(const QString& windowId, const QString& screenName, bool sticky,
                                                   int& snapX, int& snapY, int& snapWidth, int& snapHeight,
                                                   bool& shouldRestore)
{
    Q_UNUSED(screenName)
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

    // Track the assignment
    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    m_service->assignWindowToZone(windowId, result.zoneId, result.screenName, currentDesktop);

    qCDebug(lcDbusWindow) << "Restoring window" << windowId << "to persisted zone" << result.zoneId;
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
        windowObj[QStringLiteral("windowId")] = it.key();
        windowObj[QStringLiteral("x")] = it.value().x();
        windowObj[QStringLiteral("y")] = it.value().y();
        windowObj[QStringLiteral("width")] = it.value().width();
        windowObj[QStringLiteral("height")] = it.value().height();
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
    qCDebug(lcDbusWindow) << "Window" << windowId << "is now" << (floating ? "floating" : "not floating");
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

void WindowTrackingAdaptor::pushToEmptyZone()
{
    qCDebug(lcDbusWindow) << "pushToEmptyZone called";

    // Delegate to service
    QString emptyZoneId = m_service->findEmptyZone();
    if (emptyZoneId.isEmpty()) {
        qCDebug(lcDbusWindow) << "No empty zone found";
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_empty_zone"));
        return;
    }

    QRect geo = m_service->zoneGeometry(emptyZoneId, QString());
    if (!geo.isValid()) {
        qCWarning(lcDbusWindow) << "Could not get geometry for empty zone" << emptyZoneId;
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"));
        return;
    }

    qCDebug(lcDbusWindow) << "Found empty zone" << emptyZoneId << "with geometry" << geo;
    Q_EMIT moveWindowToZoneRequested(emptyZoneId, rectToJson(geo));
    Q_EMIT navigationFeedback(true, QStringLiteral("push"), QString());
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

void WindowTrackingAdaptor::snapToZoneByNumber(int zoneNumber)
{
    qCDebug(lcDbusWindow) << "snapToZoneByNumber called with zone number:" << zoneNumber;

    if (zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(lcDbusWindow) << "Invalid zone number:" << zoneNumber << "(must be 1-9)";
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"));
        return;
    }

    auto* layout = getValidatedActiveLayout(QStringLiteral("snapToZoneByNumber"));
    if (!layout) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_active_layout"));
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
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("zone_not_found"));
        return;
    }

    QString zoneId = targetZone->id().toString();
    QRect geo = m_service->zoneGeometry(zoneId, QString());
    if (!geo.isValid()) {
        qCWarning(lcDbusWindow) << "Could not get geometry for zone" << zoneNumber;
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("geometry_error"));
        return;
    }

    qCDebug(lcDbusWindow) << "Snapping to zone" << zoneNumber << "(" << zoneId << ")";
    Q_EMIT moveWindowToZoneRequested(zoneId, rectToJson(geo));
    Q_EMIT navigationFeedback(true, QStringLiteral("snap"), QString());
}

void WindowTrackingAdaptor::rotateWindowsInLayout(bool clockwise)
{
    qCDebug(lcDbusWindow) << "rotateWindowsInLayout called, clockwise:" << clockwise;

    // Delegate rotation calculation to service
    QVector<RotationEntry> rotationEntries = m_service->calculateRotation(clockwise);

    if (rotationEntries.isEmpty()) {
        auto* layout = getValidatedActiveLayout(QStringLiteral("rotateWindowsInLayout"));
        if (!layout) {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_active_layout"));
        } else if (layout->zoneCount() < 2) {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("single_zone"));
        } else {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_snapped_windows"));
        }
        return;
    }

    QJsonArray rotationArray;
    for (const RotationEntry& entry : rotationEntries) {
        QJsonObject moveObj;
        moveObj[QStringLiteral("windowId")] = entry.windowId;
        moveObj[QStringLiteral("targetZoneId")] = entry.targetZoneId;
        moveObj[QStringLiteral("x")] = entry.targetGeometry.x();
        moveObj[QStringLiteral("y")] = entry.targetGeometry.y();
        moveObj[QStringLiteral("width")] = entry.targetGeometry.width();
        moveObj[QStringLiteral("height")] = entry.targetGeometry.height();
        rotationArray.append(moveObj);
    }

    QString rotationData = QString::fromUtf8(QJsonDocument(rotationArray).toJson(QJsonDocument::Compact));
    qCInfo(lcDbusWindow) << "Rotating" << rotationArray.size() << "windows"
                         << (clockwise ? "clockwise" : "counterclockwise");
    Q_EMIT rotateWindowsRequested(clockwise, rotationData);
    Q_EMIT navigationFeedback(true, QStringLiteral("rotate"), QString());
}

void WindowTrackingAdaptor::cycleWindowsInZone(bool forward)
{
    qCDebug(lcDbusWindow) << "cycleWindowsInZone called, forward:" << forward;
    QString directive = forward ? QStringLiteral("cycle:forward") : QStringLiteral("cycle:backward");
    Q_EMIT cycleWindowsInZoneRequested(directive, QString());
}

void WindowTrackingAdaptor::reportNavigationFeedback(bool success, const QString& action, const QString& reason)
{
    qCDebug(lcDbusWindow) << "Navigation feedback: success=" << success << "action=" << action << "reason=" << reason;
    Q_EMIT navigationFeedback(success, action, reason);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Zone Geometry Queries - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

QString WindowTrackingAdaptor::findEmptyZone()
{
    // Delegate to service
    return m_service->findEmptyZone();
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

    // After layout becomes available, notify effect about pending restores
    if (!m_service->pendingZoneAssignments().isEmpty()) {
        qCDebug(lcDbusWindow) << "Layout available with" << m_service->pendingZoneAssignments().size()
                              << "pending restores - notifying effect";
        Q_EMIT pendingRestoresAvailable();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Persistence (Adaptor Responsibility: KConfig)
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::saveState()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup tracking = config->group(QStringLiteral("WindowTracking"));

    // Save zone assignments (from service state)
    QJsonObject assignmentsObj;
    for (auto it = m_service->zoneAssignments().constBegin(); it != m_service->zoneAssignments().constEnd(); ++it) {
        QString stableId = Utils::extractStableId(it.key());
        assignmentsObj[stableId] = it.value();
    }
    // Include pending assignments
    for (auto it = m_service->pendingZoneAssignments().constBegin(); it != m_service->pendingZoneAssignments().constEnd(); ++it) {
        if (!assignmentsObj.contains(it.key())) {
            assignmentsObj[it.key()] = it.value();
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

    // Save floating windows
    QJsonArray floatingArray;
    for (const QString& windowId : m_service->floatingWindows()) {
        floatingArray.append(windowId);
    }
    tracking.writeEntry(QStringLiteral("FloatingWindows"),
                        QString::fromUtf8(QJsonDocument(floatingArray).toJson(QJsonDocument::Compact)));

    // Save user-snapped classes
    QJsonArray userSnappedArray;
    for (const QString& windowClass : m_service->userSnappedClasses()) {
        userSnappedArray.append(windowClass);
    }
    tracking.writeEntry(QStringLiteral("UserSnappedClasses"),
                        QString::fromUtf8(QJsonDocument(userSnappedArray).toJson(QJsonDocument::Compact)));

    config->sync();
    qCDebug(lcDbusWindow) << "Saved state to KConfig";
}

void WindowTrackingAdaptor::loadState()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup tracking = config->group(QStringLiteral("WindowTracking"));

    // Load zone assignments into pending (keyed by stable ID)
    QHash<QString, QString> pendingZones;
    QString assignmentsJson = tracking.readEntry(QStringLiteral("WindowZoneAssignments"), QString());
    if (!assignmentsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(assignmentsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                pendingZones[it.key()] = it.value().toString();
            }
        }
    }
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

    qCDebug(lcDbusWindow) << "Loaded state from KConfig -" << pendingZones.size() << "pending zone assignments";
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
        Q_EMIT navigationFeedback(false, action, QStringLiteral("invalid_direction"));
        return false;
    }
    return true;
}

QString WindowTrackingAdaptor::rectToJson(const QRect& rect) const
{
    QJsonObject geomObj;
    geomObj[QStringLiteral("x")] = rect.x();
    geomObj[QStringLiteral("y")] = rect.y();
    geomObj[QStringLiteral("width")] = rect.width();
    geomObj[QStringLiteral("height")] = rect.height();
    return QString::fromUtf8(QJsonDocument(geomObj).toJson(QJsonDocument::Compact));
}

} // namespace PlasmaZones
