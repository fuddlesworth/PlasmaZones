// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingservice.h"
#include "interfaces.h"
#include "layout.h"
#include "zone.h"
#include "layoutmanager.h"
#include "virtualdesktopmanager.h"
#include "geometryutils.h"
#include "screenmanager.h"
#include "utils.h"
#include "logging.h"
#include <QGuiApplication>
#include <QScreen>
#include <QSet>
#include <QUuid>

namespace PlasmaZones {

WindowTrackingService::WindowTrackingService(LayoutManager* layoutManager, IZoneDetector* zoneDetector,
                                             ISettings* settings, VirtualDesktopManager* vdm, QObject* parent)
    : QObject(parent)
    , m_layoutManager(layoutManager)
    , m_zoneDetector(zoneDetector)
    , m_settings(settings)
    , m_virtualDesktopManager(vdm)
{
    Q_ASSERT(layoutManager);
    Q_ASSERT(zoneDetector);
    Q_ASSERT(settings);

    // Note: No save timer needed - persistence handled by WindowTrackingAdaptor via KConfig
    // Service just emits stateChanged() signal when state changes

    // Connect to layout changes
    connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, &WindowTrackingService::onLayoutChanged);

    // Note: Persistence is handled by WindowTrackingAdaptor via KConfig.
    // The service is a pure in-memory state manager - adaptor calls
    // populateState() after loading from KConfig.
}

WindowTrackingService::~WindowTrackingService()
{
    // Note: Persistence is handled by WindowTrackingAdaptor via KConfig
    // Service is purely in-memory state management
}

// ═══════════════════════════════════════════════════════════════════════════════
// Zone Assignment Management
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::assignWindowToZone(const QString& windowId, const QString& zoneId,
                                               const QString& screenName, int virtualDesktop)
{
    if (windowId.isEmpty() || zoneId.isEmpty()) {
        return;
    }

    // Only emit signal if value actually changed (.cursorrules compliance)
    QString previousZone = m_windowZoneAssignments.value(windowId);
    bool zoneChanged = (previousZone != zoneId);

    m_windowZoneAssignments[windowId] = zoneId;
    m_windowScreenAssignments[windowId] = screenName;
    m_windowDesktopAssignments[windowId] = virtualDesktop;

    // Also store by stable ID for session persistence
    QString stableId = Utils::extractStableId(windowId);
    m_pendingZoneAssignments[stableId] = zoneId;
    m_pendingZoneScreens[stableId] = screenName;
    m_pendingZoneDesktops[stableId] = virtualDesktop;

    if (zoneChanged) {
        Q_EMIT windowZoneChanged(windowId, zoneId);
    }
    scheduleSaveState();
}

void WindowTrackingService::unassignWindow(const QString& windowId)
{
    // Only emit signal if window was actually assigned (.cursorrules compliance)
    if (m_windowZoneAssignments.remove(windowId) == 0) {
        return; // Window wasn't assigned, nothing to do
    }

    m_windowScreenAssignments.remove(windowId);
    m_windowDesktopAssignments.remove(windowId);

    // Don't remove from pending - keep for session restore
    // (pending is keyed by stable ID anyway)

    Q_EMIT windowZoneChanged(windowId, QString());
    scheduleSaveState();
}

QString WindowTrackingService::zoneForWindow(const QString& windowId) const
{
    return m_windowZoneAssignments.value(windowId);
}

QStringList WindowTrackingService::windowsInZone(const QString& zoneId) const
{
    QStringList result;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        if (it.value() == zoneId) {
            result.append(it.key());
        }
    }
    return result;
}

QStringList WindowTrackingService::snappedWindows() const
{
    return m_windowZoneAssignments.keys();
}

bool WindowTrackingService::isWindowSnapped(const QString& windowId) const
{
    return m_windowZoneAssignments.contains(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Pre-Snap Geometry Storage
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::storePreSnapGeometry(const QString& windowId, const QRect& geometry)
{
    // Only store on FIRST snap - don't overwrite when moving A→B
    if (m_preSnapGeometries.contains(windowId)) {
        return;
    }

    if (geometry.isValid()) {
        m_preSnapGeometries[windowId] = geometry;
        scheduleSaveState();
    }
}

std::optional<QRect> WindowTrackingService::preSnapGeometry(const QString& windowId) const
{
    if (m_preSnapGeometries.contains(windowId)) {
        return m_preSnapGeometries.value(windowId);
    }
    return std::nullopt;
}

bool WindowTrackingService::hasPreSnapGeometry(const QString& windowId) const
{
    return m_preSnapGeometries.contains(windowId);
}

void WindowTrackingService::clearPreSnapGeometry(const QString& windowId)
{
    if (m_preSnapGeometries.remove(windowId) > 0) {
        scheduleSaveState();
    }
}

std::optional<QRect> WindowTrackingService::validatedPreSnapGeometry(const QString& windowId) const
{
    auto geo = preSnapGeometry(windowId);
    if (!geo) {
        return std::nullopt;
    }

    QRect rect = *geo;
    if (isGeometryOnScreen(rect)) {
        return rect;
    }

    // Adjust to fit within visible screens
    return adjustGeometryToScreen(rect);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Floating Window State
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingService::isWindowFloating(const QString& windowId) const
{
    return m_floatingWindows.contains(windowId);
}

void WindowTrackingService::setWindowFloating(const QString& windowId, bool floating)
{
    if (floating) {
        m_floatingWindows.insert(windowId);
    } else {
        m_floatingWindows.remove(windowId);
    }
    scheduleSaveState();
}

QStringList WindowTrackingService::floatingWindows() const
{
    return m_floatingWindows.values();
}

void WindowTrackingService::unsnapForFloat(const QString& windowId)
{
    QString stableId = Utils::extractStableId(windowId);

    // Save zone for restore on unfloat
    if (m_windowZoneAssignments.contains(windowId)) {
        m_preFloatZoneAssignments[stableId] = m_windowZoneAssignments.value(windowId);
        unassignWindow(windowId);
    }
}

QString WindowTrackingService::preFloatZone(const QString& windowId) const
{
    QString stableId = Utils::extractStableId(windowId);
    return m_preFloatZoneAssignments.value(stableId);
}

void WindowTrackingService::clearPreFloatZone(const QString& windowId)
{
    QString stableId = Utils::extractStableId(windowId);
    m_preFloatZoneAssignments.remove(stableId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Sticky Window Handling
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::setWindowSticky(const QString& windowId, bool sticky)
{
    m_windowStickyStates[windowId] = sticky;
}

bool WindowTrackingService::isWindowSticky(const QString& windowId) const
{
    return m_windowStickyStates.value(windowId, false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Auto-Snap Logic
// ═══════════════════════════════════════════════════════════════════════════════

SnapResult WindowTrackingService::calculateSnapToLastZone(const QString& windowId, const QString& windowScreenName,
                                                          bool isSticky) const
{
    // Check if feature is enabled
    if (!m_settings || !m_settings->moveNewWindowsToLastZone()) {
        return SnapResult::noSnap();
    }

    // Check sticky window handling
    if (isSticky && m_settings) {
        auto handling = m_settings->stickyWindowHandling();
        if (handling == StickyWindowHandling::IgnoreAll || handling == StickyWindowHandling::RestoreOnly) {
            return SnapResult::noSnap();
        }
    }

    // Need a last used zone
    if (m_lastUsedZoneId.isEmpty()) {
        return SnapResult::noSnap();
    }

    // Check if window class was ever user-snapped
    QString windowClass = Utils::extractWindowClass(windowId);
    if (!m_userSnappedClasses.contains(windowClass)) {
        return SnapResult::noSnap();
    }

    // Don't cross-screen snap
    if (!windowScreenName.isEmpty() && !m_lastUsedScreenName.isEmpty() && windowScreenName != m_lastUsedScreenName) {
        return SnapResult::noSnap();
    }

    // Check virtual desktop match (unless sticky or desktop 0 = all)
    if (!isSticky && m_virtualDesktopManager && m_lastUsedDesktop > 0) {
        int currentDesktop = m_virtualDesktopManager->currentDesktop();
        if (currentDesktop != m_lastUsedDesktop) {
            return SnapResult::noSnap();
        }
    }

    // Calculate geometry
    QRect geo = zoneGeometry(m_lastUsedZoneId, m_lastUsedScreenName);
    if (!geo.isValid()) {
        return SnapResult::noSnap();
    }

    SnapResult result;
    result.shouldSnap = true;
    result.geometry = geo;
    result.zoneId = m_lastUsedZoneId;
    result.screenName = m_lastUsedScreenName;
    return result;
}

SnapResult WindowTrackingService::calculateRestoreFromSession(const QString& windowId, const QString& screenName,
                                                              bool isSticky) const
{
    // Check sticky window handling
    if (isSticky && m_settings) {
        auto handling = m_settings->stickyWindowHandling();
        if (handling == StickyWindowHandling::IgnoreAll) {
            return SnapResult::noSnap();
        }
    }

    QString stableId = Utils::extractStableId(windowId);

    // Check for pending assignment
    if (!m_pendingZoneAssignments.contains(stableId)) {
        return SnapResult::noSnap();
    }

    QString zoneId = m_pendingZoneAssignments.value(stableId);
    QString savedScreen = m_pendingZoneScreens.value(stableId, screenName);

    // Calculate geometry
    QRect geo = zoneGeometry(zoneId, savedScreen);
    if (!geo.isValid()) {
        return SnapResult::noSnap();
    }

    SnapResult result;
    result.shouldSnap = true;
    result.geometry = geo;
    result.zoneId = zoneId;
    result.screenName = savedScreen;
    return result;
}

void WindowTrackingService::recordSnapIntent(const QString& windowId, bool wasUserInitiated)
{
    if (wasUserInitiated) {
        QString windowClass = Utils::extractWindowClass(windowId);
        if (!windowClass.isEmpty()) {
            m_userSnappedClasses.insert(windowClass);
            scheduleSaveState();
        }
    }
}

void WindowTrackingService::updateLastUsedZone(const QString& zoneId, const QString& screenName,
                                               const QString& windowClass, int virtualDesktop)
{
    m_lastUsedZoneId = zoneId;
    m_lastUsedScreenName = screenName;
    m_lastUsedZoneClass = windowClass;
    m_lastUsedDesktop = virtualDesktop;
    scheduleSaveState();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation Helpers
// ═══════════════════════════════════════════════════════════════════════════════

QString WindowTrackingService::findEmptyZone() const
{
    Layout* layout = m_layoutManager->activeLayout();
    if (!layout) {
        return QString();
    }

    // Pattern B: build occupied zones from assignments, then return first layout zone not in that set.
    QSet<QUuid> occupiedZoneIds;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        const QString& zoneId = it.value();
        if (zoneId.startsWith(QStringLiteral("zoneselector-"))) {
            continue;
        }
        auto uuid = Utils::parseUuid(zoneId);
        if (uuid) {
            occupiedZoneIds.insert(*uuid);
        } else {
            qCWarning(lcCore) << "Invalid zone ID format in assignment for window" << it.key()
                              << "- zone ID:" << zoneId;
        }
    }

    for (Zone* zone : layout->zones()) {
        if (!occupiedZoneIds.contains(zone->id())) {
            return zone->id().toString();
        }
    }
    return QString();
}

QRect WindowTrackingService::zoneGeometry(const QString& zoneId, const QString& screenName) const
{
    Zone* zone = findZoneById(zoneId);
    if (!zone) {
        return QRect();
    }

    QScreen* screen = screenName.isEmpty() ? Utils::primaryScreen() : Utils::findScreenByName(screenName);

    if (!screen) {
        screen = Utils::primaryScreen();
    }

    if (!screen) {
        return QRect();
    }

    // Use GeometryUtils to calculate absolute geometry from relative geometry
    QRectF geoF = GeometryUtils::calculateZoneGeometryInAvailableArea(zone, screen);
    return geoF.toRect();
}

QVector<RotationEntry> WindowTrackingService::calculateRotation(bool clockwise) const
{
    QVector<RotationEntry> result;

    Layout* layout = m_layoutManager->activeLayout();
    if (!layout || layout->zoneCount() < 2) {
        return result;
    }

    // Get zones sorted by zone number
    QVector<Zone*> zones = layout->zones();
    std::sort(zones.begin(), zones.end(), [](Zone* a, Zone* b) {
        return a->zoneNumber() < b->zoneNumber();
    });

    // Build window -> zone mapping for occupied zones
    QVector<QPair<QString, int>> windowZoneIndices; // windowId, current zone index
    for (int i = 0; i < zones.size(); ++i) {
        QString zoneId = zones[i]->id().toString();
        QStringList windows = windowsInZone(zoneId);
        for (const QString& windowId : windows) {
            windowZoneIndices.append({windowId, i});
        }
    }

    // Calculate rotated positions
    for (const auto& pair : windowZoneIndices) {
        int currentIdx = pair.second;
        int targetIdx = clockwise ? (currentIdx + 1) % zones.size() : (currentIdx - 1 + zones.size()) % zones.size();

        Zone* targetZone = zones[targetIdx];
        QString screenName = m_windowScreenAssignments.value(pair.first);
        QRect geo = zoneGeometry(targetZone->id().toString(), screenName);

        if (geo.isValid()) {
            RotationEntry entry;
            entry.windowId = pair.first;
            entry.targetZoneId = targetZone->id().toString();
            entry.targetGeometry = geo;
            result.append(entry);
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Resolution Change Handling
// ═══════════════════════════════════════════════════════════════════════════════

QHash<QString, QRect> WindowTrackingService::updatedWindowGeometries() const
{
    QHash<QString, QRect> result;

    if (!m_settings || !m_settings->keepWindowsInZonesOnResolutionChange()) {
        return result;
    }

    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        QString windowId = it.key();
        QString zoneId = it.value();
        QString screenName = m_windowScreenAssignments.value(windowId);

        QRect geo = zoneGeometry(zoneId, screenName);
        if (geo.isValid()) {
            result[windowId] = geo;
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::windowClosed(const QString& windowId)
{
    // Don't remove from pending assignments - keep for session restore
    // (Those are keyed by stable ID)

    m_windowZoneAssignments.remove(windowId);
    m_windowScreenAssignments.remove(windowId);
    m_windowDesktopAssignments.remove(windowId);
    m_preSnapGeometries.remove(windowId);
    m_floatingWindows.remove(windowId);
    m_windowStickyStates.remove(windowId);
    m_autoSnappedWindows.remove(windowId);

    QString stableId = Utils::extractStableId(windowId);
    m_preFloatZoneAssignments.remove(stableId);

    scheduleSaveState();
}

void WindowTrackingService::onLayoutChanged()
{
    // Validate zone assignments against new layout
    Layout* layout = m_layoutManager->activeLayout();
    if (!layout) {
        return;
    }

    // Collect valid zone IDs from new layout
    QSet<QString> validZoneIds;
    for (Zone* zone : layout->zones()) {
        validZoneIds.insert(zone->id().toString());
    }

    // Remove stale assignments
    QStringList toRemove;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        if (!validZoneIds.contains(it.value())) {
            toRemove.append(it.key());
        }
    }

    for (const QString& windowId : toRemove) {
        unassignWindow(windowId);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// State Management (persistence handled by adaptor via KConfig)
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::scheduleSaveState()
{
    // Signal to adaptor that state changed and needs saving
    // Adaptor handles actual KConfig persistence
    Q_EMIT stateChanged();
}

void WindowTrackingService::setLastUsedZone(const QString& zoneId, const QString& screenName, const QString& zoneClass,
                                            int desktop)
{
    m_lastUsedZoneId = zoneId;
    m_lastUsedScreenName = screenName;
    m_lastUsedZoneClass = zoneClass;
    m_lastUsedDesktop = desktop;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private Helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingService::isGeometryOnScreen(const QRect& geometry) const
{
    for (QScreen* screen : Utils::allScreens()) {
        QRect intersection = geometry.intersected(screen->geometry());
        if (intersection.width() >= MinVisibleWidth && intersection.height() >= MinVisibleHeight) {
            return true;
        }
    }
    return false;
}

QRect WindowTrackingService::adjustGeometryToScreen(const QRect& geometry) const
{
    // Find nearest screen
    QScreen* nearest = Utils::findNearestScreen(geometry.center());
    if (!nearest) {
        return geometry;
    }

    QRect screenGeo = nearest->geometry();
    QRect adjusted = geometry;

    // Clamp to screen bounds while preserving size where possible
    if (adjusted.right() > screenGeo.right()) {
        adjusted.moveRight(screenGeo.right());
    }
    if (adjusted.left() < screenGeo.left()) {
        adjusted.moveLeft(screenGeo.left());
    }
    if (adjusted.bottom() > screenGeo.bottom()) {
        adjusted.moveBottom(screenGeo.bottom());
    }
    if (adjusted.top() < screenGeo.top()) {
        adjusted.moveTop(screenGeo.top());
    }

    return adjusted;
}

Zone* WindowTrackingService::findZoneById(const QString& zoneId) const
{
    Layout* layout = m_layoutManager->activeLayout();
    if (!layout) {
        return nullptr;
    }

    auto uuidOpt = Utils::parseUuid(zoneId);
    if (!uuidOpt) {
        return nullptr;
    }

    return layout->zoneById(*uuidOpt);
}

} // namespace PlasmaZones
