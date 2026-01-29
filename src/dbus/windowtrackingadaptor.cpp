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
#include <QGuiApplication>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
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

    // Create business logic service (SRP: separates tracking logic from D-Bus interface)
    // Note: Service handles in-memory state; adaptor handles D-Bus + KConfig persistence
    m_service = new WindowTrackingService(layoutManager, zoneDetector, settings, virtualDesktopManager, this);

    // Forward service signals to D-Bus
    connect(m_service, &WindowTrackingService::windowZoneChanged,
            this, &WindowTrackingAdaptor::windowZoneChanged);

    // Setup debounced save timer (500ms delay to batch rapid state changes)
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(500);
    connect(m_saveTimer, &QTimer::timeout, this, &WindowTrackingAdaptor::saveState);

    // Connect to layout changes to clean up orphaned zone assignments
    // Uses LayoutManager (concrete) because ILayoutManager is a pure interface without signals
    connect(m_layoutManager, &LayoutManager::activeLayoutChanged, this, &WindowTrackingAdaptor::onLayoutChanged);

    // Load persisted window tracking state from previous session
    loadState();
}

void WindowTrackingAdaptor::windowSnapped(const QString& windowId, const QString& zoneId)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot track window snap - empty window ID";
        return;
    }

    if (zoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot track window snap - empty zone ID";
        return;
    }

    // Track window-zone assignment (only emit signal if value actually changed)
    QString previousZone = m_windowZoneAssignments.value(windowId);
    if (previousZone != zoneId) {
        m_windowZoneAssignments[windowId] = zoneId;
        Q_EMIT windowZoneChanged(windowId, zoneId);
        qCDebug(lcDbusWindow) << "Window" << windowId << "snapped to zone" << zoneId;
    }

    // BUG FIX: Clear any pending zone assignment for this stable ID
    // When user explicitly snaps a window, this should override any persisted assignment
    // from a previous session. Without this, if restoreToPersistedZone() failed (e.g., no
    // active layout at startup), the stale pending entry would cause the window to snap
    // to the WRONG zone when closed and reopened.
    QString stableId = Utils::extractStableId(windowId);
    if (m_pendingZoneAssignments.remove(stableId)) {
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
        qCDebug(lcDbusWindow) << "Cleared stale pending assignment for" << stableId;
    }

    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    if (currentDesktop > 0) {
        m_windowDesktopAssignments[windowId] = currentDesktop;
    }

    bool wasAutoSnapped = m_autoSnappedWindows.remove(windowId);

    // Track last used zone for moveNewWindowsToLastZone feature
    // Skip zone selector special IDs (they start with "zoneselector-")
    if (!zoneId.startsWith(QStringLiteral("zoneselector-")) && !wasAutoSnapped) {
        QString windowClass = Utils::extractWindowClass(windowId);
        m_lastUsedZoneId = zoneId;
        m_lastUsedZoneClass = windowClass;
        m_lastUsedDesktop = currentDesktop;
        // Try to determine which screen this zone is on by finding the zone in the layout
        if (m_layoutManager) {
            auto* layout = m_layoutManager->activeLayout();
            if (layout) {
                auto zoneUuid = Utils::parseUuid(zoneId);
                Zone* zone = zoneUuid ? layout->zoneById(*zoneUuid) : nullptr;
                if (zone) {
                    // Find which screen contains this zone's center
                    QRectF relGeom = zone->relativeGeometry();
                    for (QScreen* screen : Utils::allScreens()) {
                        QRect availGeom = ScreenManager::actualAvailableGeometry(screen);
                        QPoint zoneCenter(availGeom.x() + static_cast<int>(relGeom.center().x() * availGeom.width()),
                                          availGeom.y() + static_cast<int>(relGeom.center().y() * availGeom.height()));
                        if (screen->geometry().contains(zoneCenter)) {
                            m_lastUsedScreenName = screen->name();
                            // Track per-window screen assignment for multi-monitor resolution change handling
                            m_windowScreenAssignments[windowId] = screen->name();
                            break;
                        }
                    }
                }
            }
        }
        qCDebug(lcDbusWindow) << "Updated last used zone to" << m_lastUsedZoneId << "for class" << m_lastUsedZoneClass
                              << "on desktop" << m_lastUsedDesktop << "on screen" << m_lastUsedScreenName;
    }

    // Schedule debounced state save
    scheduleSaveState();
}

void WindowTrackingAdaptor::windowUnsnapped(const QString& windowId)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot untrack window - empty window ID";
        return;
    }

    // Get the zone this window was snapped to before removing
    QString previousZoneId = m_windowZoneAssignments.value(windowId);

    if (m_windowZoneAssignments.remove(windowId) > 0) {
        Q_EMIT windowZoneChanged(windowId, QString());
        qCDebug(lcDbusWindow) << "Window" << windowId << "unsnapped from zone" << previousZoneId;

        // Clean up per-window screen assignment
        m_windowScreenAssignments.remove(windowId);
        m_windowDesktopAssignments.remove(windowId);

        // Only clear last used zone if this window was snapped to it
        // This preserves the last used zone when unsnapping a different window
        if (!m_lastUsedZoneId.isEmpty() && previousZoneId == m_lastUsedZoneId) {
            qCDebug(lcDbusWindow) << "Clearing last used zone" << m_lastUsedZoneId;
            m_lastUsedZoneId.clear();
            m_lastUsedScreenName.clear();
            m_lastUsedZoneClass.clear();
            m_lastUsedDesktop = 0;
        }

        // Schedule debounced state save
        scheduleSaveState();
    } else {
        qCWarning(lcDbusWindow) << "Window not found for unsnap:" << windowId;
    }

    // Note: We do NOT clear pre-snap geometry here - that's handled by clearPreSnapGeometry()
    // after the caller has applied the restored geometry. This allows the caller to decide
    // whether to restore (unsnap to free space) or not (snap to another zone A→B).
}

void WindowTrackingAdaptor::setWindowSticky(const QString& windowId, bool sticky)
{
    if (windowId.isEmpty()) {
        return;
    }

    if (sticky) {
        m_windowStickyStates[windowId] = true;
    } else {
        m_windowStickyStates.remove(windowId);
    }
}
void WindowTrackingAdaptor::windowUnsnappedForFloat(const QString& windowId)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot prepare float - empty window ID";
        return;
    }

    QString stableId = Utils::extractStableId(windowId);
    QString previousZoneId = m_windowZoneAssignments.value(windowId);

    if (previousZoneId.isEmpty()) {
        // Window was not snapped - no-op. Avoids "Window not found for unsnap" when
        // floating a never-snapped window.
        return;
    }

    // Save zone so we can restore when user unfloats
    m_preFloatZoneAssignments[stableId] = previousZoneId;

    m_windowZoneAssignments.remove(windowId);
    m_windowScreenAssignments.remove(windowId);
    m_windowDesktopAssignments.remove(windowId);
    Q_EMIT windowZoneChanged(windowId, QString());
    qCDebug(lcDbusWindow) << "Window" << windowId << "unsnapped for float from zone" << previousZoneId
                          << "(will restore on unfloat)";

    if (!m_lastUsedZoneId.isEmpty() && previousZoneId == m_lastUsedZoneId) {
        qCDebug(lcDbusWindow) << "Clearing last used zone" << m_lastUsedZoneId;
        m_lastUsedZoneId.clear();
        m_lastUsedScreenName.clear();
        m_lastUsedZoneClass.clear();
        m_lastUsedDesktop = 0;
    }

    scheduleSaveState();
}

bool WindowTrackingAdaptor::getPreFloatZone(const QString& windowId, QString& zoneIdOut)
{
    if (windowId.isEmpty()) {
        zoneIdOut.clear();
        return false;
    }
    zoneIdOut = m_preFloatZoneAssignments.value(Utils::extractStableId(windowId));
    return !zoneIdOut.isEmpty();
}

void WindowTrackingAdaptor::clearPreFloatZone(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    if (m_preFloatZoneAssignments.remove(Utils::extractStableId(windowId)) > 0) {
        qCDebug(lcDbusWindow) << "Cleared pre-float zone for window" << windowId;
        scheduleSaveState();
    }
}

void WindowTrackingAdaptor::storePreSnapGeometry(const QString& windowId, int x, int y, int width, int height)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot store pre-snap geometry - empty window ID";
        return;
    }

    // Key design: Only store on FIRST snap - don't overwrite when moving A→B
    // This preserves the true "original" geometry from before any snapping
    if (m_preSnapGeometries.contains(windowId)) {
        qCDebug(lcDbusWindow) << "Pre-snap geometry already stored for window" << windowId
                              << "- keeping original (A→B snap scenario)";
        return;
    }

    // Validate geometry (must have positive dimensions)
    if (width <= 0 || height <= 0) {
        qCWarning(lcDbusWindow) << "Invalid geometry for pre-snap storage:"
                                << "width=" << width << "height=" << height;
        return;
    }

    QRect geometry(x, y, width, height);
    m_preSnapGeometries[windowId] = geometry;
    qCDebug(lcDbusWindow) << "Stored pre-snap geometry for window" << windowId << "at" << geometry;

    // Schedule debounced state save
    scheduleSaveState();
}

bool WindowTrackingAdaptor::getPreSnapGeometry(const QString& windowId, int& x, int& y, int& width, int& height)
{
    // Initialize outputs to 0
    x = 0;
    y = 0;
    width = 0;
    height = 0;

    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot get pre-snap geometry - empty window ID";
        return false;
    }

    auto it = m_preSnapGeometries.constFind(windowId);
    if (it == m_preSnapGeometries.constEnd()) {
        qCDebug(lcDbusWindow) << "No pre-snap geometry stored for window" << windowId;
        return false;
    }

    const QRect& geometry = it.value();
    x = geometry.x();
    y = geometry.y();
    width = geometry.width();
    height = geometry.height();

    qCDebug(lcDbusWindow) << "Retrieved pre-snap geometry for window" << windowId << "at" << geometry;
    return true;
}

bool WindowTrackingAdaptor::hasPreSnapGeometry(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    return m_preSnapGeometries.contains(windowId);
}

void WindowTrackingAdaptor::clearPreSnapGeometry(const QString& windowId)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot clear pre-snap geometry - empty window ID";
        return;
    }

    if (m_preSnapGeometries.remove(windowId) > 0) {
        qCDebug(lcDbusWindow) << "Cleared pre-snap geometry for window" << windowId;
        // Schedule debounced state save
        scheduleSaveState();
    }
}

void WindowTrackingAdaptor::windowClosed(const QString& windowId)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot clean up closed window - empty window ID";
        return;
    }

    // Persist the zone before removing. saveState() only reads m_windowZoneAssignments; once we
    // remove the window its zone would be lost. Storing (stableId, zoneId) in
    // m_pendingZoneAssignments ensures it's included when saveState() runs.
    QString zoneId = m_windowZoneAssignments.value(windowId);
    if (!zoneId.isEmpty() && !zoneId.startsWith(QStringLiteral("zoneselector-"))) {
        QString stableId = Utils::extractStableId(windowId);
        if (!stableId.isEmpty()) {
            m_pendingZoneAssignments[stableId] = zoneId;
            QString screenName = m_windowScreenAssignments.value(windowId);
            if (!screenName.isEmpty()) {
                m_pendingZoneScreens[stableId] = screenName;
            } else {
                m_pendingZoneScreens.remove(stableId);
            }
            int desktop = m_windowDesktopAssignments.value(windowId, 0);
            if (desktop <= 0 && m_virtualDesktopManager) {
                desktop = m_virtualDesktopManager->currentDesktop();
            }
            if (desktop > 0) {
                m_pendingZoneDesktops[stableId] = desktop;
            } else {
                m_pendingZoneDesktops.remove(stableId);
            }
            qCDebug(lcDbusWindow) << "Persisted zone" << zoneId << "for closed window" << stableId
                                  << "(for restore on reopen)";
        }
    }

    bool hadZoneAssignment = m_windowZoneAssignments.remove(windowId) > 0;
    bool hadScreenAssignment = m_windowScreenAssignments.remove(windowId) > 0;
    bool hadDesktopAssignment = m_windowDesktopAssignments.remove(windowId) > 0;
    bool hadStickyState = m_windowStickyStates.remove(windowId);
    bool hadPreSnapGeometry = m_preSnapGeometries.remove(windowId) > 0;
    // Use stable ID for floating windows (they're stored by stable ID)
    QString stableId = Utils::extractStableId(windowId);
    bool hadFloatingState = m_floatingWindows.remove(stableId);
    bool hadPreFloatZone = m_preFloatZoneAssignments.remove(stableId) > 0;

    if (hadZoneAssignment || hadScreenAssignment || hadDesktopAssignment || hadStickyState || hadPreSnapGeometry
        || hadFloatingState || hadPreFloatZone) {
        qCDebug(lcDbusWindow) << "Cleaned up tracking data for closed window" << windowId
                              << "(zone:" << hadZoneAssignment << ", screen:" << hadScreenAssignment
                              << ", desktop:" << hadDesktopAssignment << ", sticky:" << hadStickyState
                              << ", presnap:" << hadPreSnapGeometry << ", floating:" << hadFloatingState
                              << ", prefloat:" << hadPreFloatZone << ")";
        // Schedule debounced state save
        scheduleSaveState();
    }

    // Phase 2.1: Emit window removed event for autotiling consumers
    Q_EMIT windowRemovedEvent(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 2.1: Window Event Methods for Autotiling
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::windowAdded(const QString& windowId, const QString& screenName)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot process windowAdded - empty window ID";
        return;
    }

    qCDebug(lcDbusWindow) << "Window added:" << windowId << "on screen" << screenName;

    // Emit signal for autotiling consumers (AutotileEngine)
    Q_EMIT windowAddedEvent(windowId, screenName);
}

void WindowTrackingAdaptor::windowActivated(const QString& windowId, const QString& screenName)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot process windowActivated - empty window ID";
        return;
    }

    qCDebug(lcDbusWindow) << "Window activated:" << windowId << "on screen" << screenName;

    // Update last-used zone when focusing a snapped window
    // This makes "move new windows to last zone" more intuitive - clicking a window
    // in zone 3 means new windows of that class will go to zone 3
    QString zoneId = m_windowZoneAssignments.value(windowId);
    if (!zoneId.isEmpty() && m_settings && m_settings->moveNewWindowsToLastZone()) {
        // Only update from user-focused windows, not auto-snapped ones
        if (!m_autoSnappedWindows.contains(windowId)) {
            QString windowClass = Utils::extractWindowClass(windowId);
            int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;

            m_lastUsedZoneId = zoneId;
            m_lastUsedScreenName = screenName;
            m_lastUsedZoneClass = windowClass;
            if (currentDesktop > 0) {
                m_lastUsedDesktop = currentDesktop;
            }

            qCDebug(lcDbusWindow) << "Updated last-used zone to" << zoneId
                                  << "from focus on" << windowClass
                                  << "screen" << screenName;
        }
    }

    // Emit signal for autotiling consumers (AutotileEngine)
    Q_EMIT windowActivatedEvent(windowId, screenName);
}

bool WindowTrackingAdaptor::isGeometryOnScreen(int x, int y, int width, int height) const
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    QRect geometry(x, y, width, height);

    // Check if geometry has sufficient visible area on any screen
    // A window must have at least MinVisibleWidth x MinVisibleHeight pixels visible
    // to be considered "on screen" (prevents windows barely clipping a screen corner)
    for (QScreen* screen : Utils::allScreens()) {
        QRect intersection = screen->geometry().intersected(geometry);
        if (intersection.width() >= MinVisibleWidth && intersection.height() >= MinVisibleHeight) {
            return true;
        }
    }

    return false;
}

bool WindowTrackingAdaptor::getValidatedPreSnapGeometry(const QString& windowId, int& x, int& y, int& width,
                                                        int& height)
{
    // First, get the raw stored geometry
    if (!getPreSnapGeometry(windowId, x, y, width, height)) {
        return false;
    }

    // Check if geometry is still valid on any screen
    if (isGeometryOnScreen(x, y, width, height)) {
        qCDebug(lcDbusWindow) << "Pre-snap geometry is valid on screen";
        return true;
    }

    // Geometry is off-screen (monitor disconnected, resolution changed, etc.)
    // Find the nearest screen and adjust position to fit
    qCDebug(lcDbusWindow) << "Pre-snap geometry off-screen, adjusting...";

    // Find screen closest to the original geometry center
    QPoint originalCenter(x + width / 2, y + height / 2);
    QScreen* nearestScreen = Utils::findNearestScreen(originalCenter);
    if (!nearestScreen) {
        qCWarning(lcDbusWindow) << "No screens available for geometry validation";
        return false;
    }

    // Adjust geometry to fit within the nearest screen
    QRect screenGeom = nearestScreen->availableGeometry();

    // Preserve original size if possible, otherwise clamp to screen size
    int newWidth = qMin(width, screenGeom.width());
    int newHeight = qMin(height, screenGeom.height());

    // Position within screen bounds
    int newX = qBound(screenGeom.x(), x, screenGeom.right() - newWidth + 1);
    int newY = qBound(screenGeom.y(), y, screenGeom.bottom() - newHeight + 1);

    qCDebug(lcDbusWindow) << "Adjusted geometry from" << QRect(x, y, width, height) << "to"
                          << QRect(newX, newY, newWidth, newHeight);

    x = newX;
    y = newY;
    width = newWidth;
    height = newHeight;

    return true;
}

QString WindowTrackingAdaptor::getZoneForWindow(const QString& windowId)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot get zone for window - empty window ID";
        return QString();
    }

    return m_windowZoneAssignments.value(windowId);
}

QStringList WindowTrackingAdaptor::getWindowsInZone(const QString& zoneId)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot get windows in zone - empty zone ID";
        return QStringList();
    }

    // Normalize zone ID comparison so callers get consistent results regardless of
    // format (with/without braces). Zone IDs can come from layout (Zone::id().toString()),
    // D-Bus (effect), or JSON; QUuid::fromString accepts both formats.
    std::optional<QUuid> queryUuid;
    bool useUuidCompare = false;
    if (!zoneId.startsWith(QStringLiteral("zoneselector-"))) {
        queryUuid = Utils::parseUuid(zoneId);
        useUuidCompare = queryUuid.has_value();
    }

    QStringList windows;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        const QString& storedZoneId = it.value();
        bool match = false;
        if (useUuidCompare && queryUuid) {
            auto storedUuid = Utils::parseUuid(storedZoneId);
            match = storedUuid && *storedUuid == *queryUuid;
        } else {
            match = (storedZoneId == zoneId);
        }
        if (match) {
            windows.append(it.key());
        }
    }
    return windows;
}

QStringList WindowTrackingAdaptor::getSnappedWindows()
{
    return m_windowZoneAssignments.keys();
}

QStringList WindowTrackingAdaptor::getMultiZoneForWindow(const QString& windowId)
{
    QStringList zoneIds;

    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot get multi-zone for window - empty window ID";
        return zoneIds;
    }

    // Get primary zone ID
    QString primaryZoneId = m_windowZoneAssignments.value(windowId);
    if (primaryZoneId.isEmpty()) {
        // Window not found or not snapped
        return zoneIds;
    }

    // Get active layout
    if (!m_layoutManager) {
        qCWarning(lcDbusWindow) << "Cannot get multi-zone - no layout manager";
        return zoneIds;
    }

    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        qCWarning(lcDbusWindow) << "Cannot get multi-zone - no active layout";
        return zoneIds;
    }

    // Find primary zone
    auto primaryUuid = Utils::parseUuid(primaryZoneId);
    if (!primaryUuid) {
        qCWarning(lcDbusWindow) << "Invalid zone ID format:" << primaryZoneId;
        return zoneIds;
    }

    Zone* primaryZone = layout->zoneById(*primaryUuid);
    if (!primaryZone) {
        qCWarning(lcDbusWindow) << "Primary zone not found:" << primaryZoneId;
        return zoneIds;
    }

    // Add primary zone ID first
    zoneIds.append(primaryZoneId);

    // Set layout on detector for adjacency calculation
    if (m_zoneDetector) {
        m_zoneDetector->setLayout(layout);

        // Get primary zone center point
        QRectF primaryGeom = primaryZone->geometry();
        QPointF centerPoint(primaryGeom.center().x(), primaryGeom.center().y());

        // Find zones near the primary zone edge (adjacent zones)
        QVector<Zone*> nearbyZones = m_zoneDetector->zonesNearEdge(centerPoint);

        // Filter to only zones that share an edge with primary zone
        for (Zone* zone : nearbyZones) {
            if (zone && zone != primaryZone) {
                // Check if zones are adjacent (share an edge)
                const QRectF& r1 = primaryZone->geometry();
                const QRectF& r2 = zone->geometry();
                const qreal threshold = 20.0; // Adjacency threshold

                bool isAdjacent = false;

                // Check horizontal adjacency (left-right)
                if ((qAbs(r1.right() - r2.left()) <= threshold || qAbs(r2.right() - r1.left()) <= threshold)) {
                    // Check vertical overlap
                    if (r1.top() < r2.bottom() && r1.bottom() > r2.top()) {
                        isAdjacent = true;
                    }
                }

                // Check vertical adjacency (top-bottom)
                if (!isAdjacent
                    && (qAbs(r1.bottom() - r2.top()) <= threshold || qAbs(r2.bottom() - r1.top()) <= threshold)) {
                    // Check horizontal overlap
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
    return m_lastUsedZoneId;
}

void WindowTrackingAdaptor::snapToLastZone(const QString& windowId, const QString& windowScreenName, bool sticky,
                                           int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    // Initialize output parameters
    snapX = 0;
    snapY = 0;
    snapWidth = 0;
    snapHeight = 0;
    shouldSnap = false;

    // Check if feature is enabled
    if (!m_settings || !m_settings->moveNewWindowsToLastZone()) {
        qCDebug(lcDbusWindow) << "moveNewWindowsToLastZone disabled";
        return;
    }

    // Check if we have a last used zone
    if (m_lastUsedZoneId.isEmpty()) {
        qCDebug(lcDbusWindow) << "No last used zone to snap to";
        return;
    }

    StickyWindowHandling stickyHandling =
        m_settings ? m_settings->stickyWindowHandling() : StickyWindowHandling::TreatAsNormal;
    if (sticky && stickyHandling != StickyWindowHandling::TreatAsNormal) {
        qCDebug(lcDbusWindow) << "Sticky window - auto-snap disabled by setting";
        return;
    }

    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    if (m_lastUsedDesktop <= 0 || currentDesktop <= 0) {
        qCDebug(lcDbusWindow) << "Missing desktop info for auto-snap"
                              << "(window:" << currentDesktop << "last:" << m_lastUsedDesktop << ")";
        return;
    }
    if (currentDesktop != m_lastUsedDesktop) {
        qCDebug(lcDbusWindow) << "Window on desktop" << currentDesktop << "but last zone on desktop"
                              << m_lastUsedDesktop << "- skipping cross-desktop snap";
        return;
    }

    if (m_lastUsedScreenName.isEmpty() || windowScreenName.isEmpty()) {
        qCDebug(lcDbusWindow) << "Missing screen info for auto-snap"
                              << "(window:" << windowScreenName << "last:" << m_lastUsedScreenName << ")";
        return;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // BUG FIX #1: Prevent cross-monitor snapping
    // Only snap if window is on the SAME screen as the last used zone
    // This prevents windows on Monitor 2 from being snapped to zones on Monitor 1
    // ═══════════════════════════════════════════════════════════════════════════
    if (windowScreenName != m_lastUsedScreenName) {
        qCDebug(lcDbusWindow) << "Window on screen" << windowScreenName << "but last zone on screen"
                              << m_lastUsedScreenName << "- skipping cross-monitor snap";
        return;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // BUG FIX #2: Only auto-snap windows whose class was USER-snapped before
    // This prevents auto-snapping windows that were never manually snapped by the user.
    // ═══════════════════════════════════════════════════════════════════════════
    QString windowClass = Utils::extractWindowClass(windowId);
    if (!windowClass.isEmpty() && !m_userSnappedClasses.contains(windowClass)) {
        qCDebug(lcDbusWindow) << "Window class" << windowClass << "was never user-snapped - skipping auto-snap";
        return;
    }

    // Only auto-snap if the last used zone belongs to this class
    if (!m_lastUsedZoneClass.isEmpty() && windowClass != m_lastUsedZoneClass) {
        qCDebug(lcDbusWindow) << "Last used zone belongs to class" << m_lastUsedZoneClass << "- skipping auto-snap for"
                              << windowClass;
        return;
    }

    // Get active layout
    if (!m_layoutManager) {
        qCWarning(lcDbusWindow) << "No layout manager";
        return;
    }

    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        qCWarning(lcDbusWindow) << "No active layout";
        return;
    }

    // Find the zone
    auto zoneUuid = Utils::parseUuid(m_lastUsedZoneId);
    if (!zoneUuid) {
        qCWarning(lcDbusWindow) << "Invalid last used zone ID:" << m_lastUsedZoneId;
        return;
    }

    Zone* zone = layout->zoneById(*zoneUuid);
    if (!zone) {
        qCWarning(lcDbusWindow) << "Last used zone not found in current layout:" << m_lastUsedZoneId;
        return;
    }

    // Use the WINDOW's screen for geometry calculation, not the last used screen
    QScreen* targetScreen = Utils::findScreenByName(windowScreenName);
    if (!targetScreen) {
        // Fall back to last used screen, then primary
        targetScreen = Utils::findScreenByName(m_lastUsedScreenName);
    }
    if (!targetScreen) {
        targetScreen = Utils::primaryScreen();
    }
    if (!targetScreen) {
        qCWarning(lcDbusWindow) << "No screen available";
        return;
    }

    // Recalculate zone geometry for target screen
    layout->recalculateZoneGeometries(ScreenManager::actualAvailableGeometry(targetScreen));

    // Get zone geometry with gaps
    // Use per-layout zonePadding/outerGap if set, otherwise fall back to global setting
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
    int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);
    QRectF zoneGeom = GeometryUtils::getZoneGeometryWithGaps(zone, targetScreen, zonePadding, outerGap, true);
    if (!zoneGeom.isValid()) {
        qCWarning(lcDbusWindow) << "Invalid zone geometry";
        return;
    }

    QRect snapGeom = zoneGeom.toRect();
    snapX = snapGeom.x();
    snapY = snapGeom.y();
    snapWidth = snapGeom.width();
    snapHeight = snapGeom.height();
    shouldSnap = true;
    m_autoSnappedWindows.insert(windowId);

    // Track this window as snapped to the zone (only emit signal if value changed)
    QString previousZone = m_windowZoneAssignments.value(windowId);
    if (previousZone != m_lastUsedZoneId) {
        m_windowZoneAssignments[windowId] = m_lastUsedZoneId;
        // Track screen assignment for multi-monitor resolution change handling
        m_windowScreenAssignments[windowId] = targetScreen->name();
        if (currentDesktop > 0) {
            m_windowDesktopAssignments[windowId] = currentDesktop;
        }
        Q_EMIT windowZoneChanged(windowId, m_lastUsedZoneId);
        // Schedule debounced state save
        scheduleSaveState();
    }

    qCDebug(lcDbusWindow) << "Snapping new window" << windowId << "to last used zone" << m_lastUsedZoneId << "at"
                          << snapGeom << "on screen" << targetScreen->name();
}

void WindowTrackingAdaptor::restoreToPersistedZone(const QString& windowId, const QString& screenName, bool sticky,
                                                   int& snapX, int& snapY, int& snapWidth, int& snapHeight,
                                                   bool& shouldRestore)
{
    Q_UNUSED(screenName)
    // Initialize outputs
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldRestore = false;

    if (windowId.isEmpty()) {
        return;
    }

    // Extract stable ID (windowClass:resourceName without pointer address)
    QString stableId = Utils::extractStableId(windowId);
    if (stableId.isEmpty()) {
        return;
    }

    // Check if we have a pending zone assignment from the previous session
    if (!m_pendingZoneAssignments.contains(stableId)) {
        qCDebug(lcDbusWindow) << "No persisted zone for" << stableId;
        return;
    }

    QString persistedZoneId = m_pendingZoneAssignments.value(stableId);
    QString pendingScreenName = m_pendingZoneScreens.value(stableId);
    int pendingDesktop = m_pendingZoneDesktops.value(stableId, 0);
    if (persistedZoneId.isEmpty()) {
        m_pendingZoneAssignments.remove(stableId);
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
        return;
    }

    StickyWindowHandling stickyHandling =
        m_settings ? m_settings->stickyWindowHandling() : StickyWindowHandling::TreatAsNormal;
    if (sticky && stickyHandling == StickyWindowHandling::IgnoreAll) {
        qCDebug(lcDbusWindow) << "Sticky window restore disabled by setting for" << stableId;
        m_pendingZoneAssignments.remove(stableId);
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
        return;
    }

    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    if (pendingDesktop <= 0 || currentDesktop <= 0) {
        qCDebug(lcDbusWindow) << "Missing desktop info for persisted restore of" << stableId;
        m_pendingZoneAssignments.remove(stableId);
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
        return;
    }
    bool allowCrossDesktopRestore = sticky && stickyHandling == StickyWindowHandling::RestoreOnly;
    if (!allowCrossDesktopRestore && currentDesktop != pendingDesktop) {
        qCDebug(lcDbusWindow) << "Persisted zone for" << stableId << "is on desktop" << pendingDesktop
                              << "- skipping restore on desktop" << currentDesktop;
        return;
    }

    qCDebug(lcDbusWindow) << "Found persisted zone" << persistedZoneId << "for" << stableId;

    // Get active layout
    if (!m_layoutManager) {
        qCWarning(lcDbusWindow) << "No layout manager for persisted restore";
        // BUG FIX: Remove pending entry to prevent stale data causing wrong zone on retry
        // This can happen during startup race conditions. The assignment will be re-saved
        // correctly when the user manually snaps the window.
        m_pendingZoneAssignments.remove(stableId);
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
        return;
    }

    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        qCWarning(lcDbusWindow) << "No active layout for persisted restore";
        // BUG FIX: Remove pending entry to prevent stale data causing wrong zone on retry
        m_pendingZoneAssignments.remove(stableId);
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
        return;
    }

    // Find the zone
    auto zoneUuid = Utils::parseUuid(persistedZoneId);
    if (!zoneUuid) {
        qCWarning(lcDbusWindow) << "Invalid zone UUID:" << persistedZoneId;
        m_pendingZoneAssignments.remove(stableId);
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
        return;
    }

    Zone* zone = layout->zoneById(*zoneUuid);
    if (!zone) {
        qCWarning(lcDbusWindow) << "Zone not found in current layout:" << persistedZoneId;
        m_pendingZoneAssignments.remove(stableId);
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
        return;
    }

    // Determine target screen - use provided screen name or primary
    QScreen* targetScreen = nullptr;
    if (!pendingScreenName.isEmpty()) {
        targetScreen = Utils::findScreenByName(pendingScreenName);
    }
    if (!targetScreen) {
        qCDebug(lcDbusWindow) << "Missing screen info for persisted restore of" << stableId;
        m_pendingZoneAssignments.remove(stableId);
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
        return;
    }

    // Recalculate zone geometry for target screen
    layout->recalculateZoneGeometries(ScreenManager::actualAvailableGeometry(targetScreen));

    // Get zone geometry with gaps
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
    int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);
    QRectF zoneGeom = GeometryUtils::getZoneGeometryWithGaps(zone, targetScreen, zonePadding, outerGap, true);
    if (!zoneGeom.isValid()) {
        qCWarning(lcDbusWindow) << "Invalid zone geometry for persisted restore";
        return;
    }

    QRect snapGeom = zoneGeom.toRect();
    snapX = snapGeom.x();
    snapY = snapGeom.y();
    snapWidth = snapGeom.width();
    snapHeight = snapGeom.height();
    shouldRestore = true;
    m_autoSnappedWindows.insert(windowId);

    // Track this window as snapped to the persisted zone
    m_windowZoneAssignments[windowId] = persistedZoneId;
    // Track screen assignment for multi-monitor resolution change handling
    m_windowScreenAssignments[windowId] = targetScreen->name();
    if (currentDesktop > 0) {
        m_windowDesktopAssignments[windowId] = currentDesktop;
    }
    Q_EMIT windowZoneChanged(windowId, persistedZoneId);

    // Remove from pending - we've successfully restored this window
    m_pendingZoneAssignments.remove(stableId);
    m_pendingZoneScreens.remove(stableId);
    m_pendingZoneDesktops.remove(stableId);

    // Schedule debounced state save
    scheduleSaveState();

    qCDebug(lcDbusWindow) << "Restoring window" << windowId << "(stable:" << stableId << ")"
                          << "to persisted zone" << persistedZoneId << "at" << snapGeom;
}

QString WindowTrackingAdaptor::getUpdatedWindowGeometries()
{
    // Check if feature is enabled
    if (!m_settings || !m_settings->keepWindowsInZonesOnResolutionChange()) {
        return QStringLiteral("[]");
    }

    if (m_windowZoneAssignments.isEmpty()) {
        return QStringLiteral("[]");
    }

    // Get active layout
    if (!m_layoutManager) {
        qCWarning(lcDbusWindow) << "No layout manager for geometry update";
        return QStringLiteral("[]");
    }

    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        qCWarning(lcDbusWindow) << "No active layout for geometry update";
        return QStringLiteral("[]");
    }

    QJsonArray windowGeometries;

    // For each tracked window, calculate its current zone geometry
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        QString windowId = it.key();
        QString zoneId = it.value();

        // Skip zone selector special IDs
        if (zoneId.startsWith(QStringLiteral("zoneselector-"))) {
            continue;
        }

        // Find the zone
        auto zoneUuidOpt = Utils::parseUuid(zoneId);
        if (!zoneUuidOpt) {
            continue;
        }

        Zone* zone = layout->zoneById(*zoneUuidOpt);
        if (!zone) {
            continue;
        }

        // Use stored screen assignment for multi-monitor support
        // Fall back to primary screen if window's screen is unknown or disconnected
        QScreen* targetScreen = nullptr;
        QString storedScreenName = m_windowScreenAssignments.value(windowId);
        if (!storedScreenName.isEmpty()) {
            targetScreen = Utils::findScreenByName(storedScreenName);
        }
        if (!targetScreen) {
            targetScreen = Utils::primaryScreen();
        }
        if (!targetScreen) {
            continue;
        }

        layout->recalculateZoneGeometries(ScreenManager::actualAvailableGeometry(targetScreen));
        // Use per-layout zonePadding/outerGap if set, otherwise fall back to global setting
        int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
        int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);
        QRectF zoneGeom = GeometryUtils::getZoneGeometryWithGaps(zone, targetScreen, zonePadding, outerGap, true);

        if (zoneGeom.isValid()) {
            QJsonObject windowObj;
            windowObj[QStringLiteral("windowId")] = windowId;
            windowObj[QStringLiteral("x")] = static_cast<int>(zoneGeom.x());
            windowObj[QStringLiteral("y")] = static_cast<int>(zoneGeom.y());
            windowObj[QStringLiteral("width")] = static_cast<int>(zoneGeom.width());
            windowObj[QStringLiteral("height")] = static_cast<int>(zoneGeom.height());
            windowGeometries.append(windowObj);
        }
    }

    if (windowGeometries.isEmpty()) {
        return QStringLiteral("[]");
    }

    qCDebug(lcDbusWindow) << "Returning updated geometries for" << windowGeometries.size() << "windows";

    return QString::fromUtf8(QJsonDocument(windowGeometries).toJson(QJsonDocument::Compact));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 1 Keyboard Navigation Implementation
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::moveWindowToAdjacentZone(const QString& direction)
{
    qCDebug(lcDbusWindow) << "moveWindowToAdjacentZone called with direction:" << direction;

    if (direction.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot move - empty direction";
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("invalid_direction"));
        return;
    }

    // This method is called by the daemon when a shortcut is pressed.
    // We emit a signal that the KWin script listens to.
    // The KWin script will:
    //   1. Get the focused window ID
    //   2. Call getZoneForWindow() to find its current zone
    //   3. Call ZoneDetectionAdaptor::getAdjacentZone() to find target zone
    //   4. Move the window to the target zone geometry

    // For now, emit the signal with the direction - KWin effect handles the rest
    // We'll encode direction as a special "directive" that KWin interprets
    Q_EMIT moveWindowToZoneRequested(QStringLiteral("navigate:") + direction, QString());
    // Note: Success/failure feedback will be emitted by the KWin effect after it
    // attempts the navigation, since only it knows if the move succeeded
}

void WindowTrackingAdaptor::focusAdjacentZone(const QString& direction)
{
    qCDebug(lcDbusWindow) << "focusAdjacentZone called with direction:" << direction;

    if (direction.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot focus - empty direction";
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("invalid_direction"));
        return;
    }

    // Similar to moveWindowToAdjacentZone, emit signal for KWin effect
    Q_EMIT focusWindowInZoneRequested(QStringLiteral("navigate:") + direction, QString());
    // Note: Success/failure feedback will be emitted by the KWin effect
}

void WindowTrackingAdaptor::pushToEmptyZone()
{
    qCDebug(lcDbusWindow) << "pushToEmptyZone called";

    QString emptyZoneId = findEmptyZone();
    if (emptyZoneId.isEmpty()) {
        qCDebug(lcDbusWindow) << "No empty zone found";
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_empty_zone"));
        return;
    }

    QString geometry = getZoneGeometry(emptyZoneId);
    if (geometry.isEmpty()) {
        qCWarning(lcDbusWindow) << "Could not get geometry for empty zone" << emptyZoneId;
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"));
        return;
    }

    qCDebug(lcDbusWindow) << "Found empty zone" << emptyZoneId << "with geometry" << geometry;
    Q_EMIT moveWindowToZoneRequested(emptyZoneId, geometry);
    Q_EMIT navigationFeedback(true, QStringLiteral("push"), QString());
}

void WindowTrackingAdaptor::restoreWindowSize()
{
    qCDebug(lcDbusWindow) << "restoreWindowSize called";
    Q_EMIT restoreWindowRequested();
    // Note: Success/failure feedback will be emitted by the KWin effect after it
    // attempts the restore, since only it knows if pre-snap geometry exists
}

void WindowTrackingAdaptor::toggleWindowFloat()
{
    qCDebug(lcDbusWindow) << "toggleWindowFloat called";
    // Signal KWin effect to toggle float state for the focused window
    // The effect will determine current state and toggle it
    // We pass 'true' as a "toggle request" signal - the effect handles the actual toggle logic
    Q_EMIT toggleWindowFloatRequested(true);
    // Note: Success/failure feedback will be emitted by the KWin effect
}

void WindowTrackingAdaptor::swapWindowWithAdjacentZone(const QString& direction)
{
    qCDebug(lcDbusWindow) << "swapWindowWithAdjacentZone called with direction:" << direction;

    if (direction.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot swap - empty direction";
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_direction"));
        return;
    }

    // This method is called by the daemon when a swap shortcut is pressed.
    // We emit a signal that the KWin script listens to.
    // The KWin script will:
    //   1. Get the focused window ID and its current zone
    //   2. Call ZoneDetectionAdaptor::getAdjacentZone() to find target zone
    //   3. Find any window in the target zone
    //   4. Swap the positions of both windows (each gets the other's zone geometry)
    //   5. If target zone is empty, just move the window like regular navigation

    // Emit the signal with the direction - KWin effect handles the swap logic
    // We use "swap:" prefix to distinguish from regular "navigate:" moves
    Q_EMIT swapWindowsRequested(QStringLiteral("swap:") + direction, QString(), QString());
    // Note: Success/failure feedback will be emitted by the KWin effect after it
    // attempts the swap, since only it knows if windows exist to swap
}

void WindowTrackingAdaptor::snapToZoneByNumber(int zoneNumber)
{
    qCDebug(lcDbusWindow) << "snapToZoneByNumber called with zone number:" << zoneNumber;

    if (zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(lcDbusWindow) << "Invalid zone number:" << zoneNumber << "(must be 1-9)";
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"));
        return;
    }

    if (!m_layoutManager) {
        qCWarning(lcDbusWindow) << "No layout manager available";
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_layout_manager"));
        return;
    }

    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        qCDebug(lcDbusWindow) << "No active layout";
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_active_layout"));
        return;
    }

    // Find zone with matching zoneNumber
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

    // Use default toString() format (with braces) to match m_windowZoneAssignments
    QString zoneId = targetZone->id().toString();
    QString geometry = getZoneGeometry(zoneId);
    if (geometry.isEmpty()) {
        qCWarning(lcDbusWindow) << "Could not get geometry for zone" << zoneNumber;
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("geometry_error"));
        return;
    }

    qCDebug(lcDbusWindow) << "Snapping to zone" << zoneNumber << "(" << zoneId << ") with geometry" << geometry;
    Q_EMIT moveWindowToZoneRequested(zoneId, geometry);
    Q_EMIT navigationFeedback(true, QStringLiteral("snap"), QString());
}

void WindowTrackingAdaptor::rotateWindowsInLayout(bool clockwise)
{
    qCDebug(lcDbusWindow) << "rotateWindowsInLayout called, clockwise:" << clockwise;

    if (!m_layoutManager) {
        qCWarning(lcDbusWindow) << "No layout manager available";
        Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_layout_manager"));
        return;
    }

    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        qCDebug(lcDbusWindow) << "No active layout";
        Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_active_layout"));
        return;
    }

    const auto& zones = layout->zones();
    if (zones.isEmpty()) {
        qCDebug(lcDbusWindow) << "Layout has no zones";
        Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_zones"));
        return;
    }

    // Single zone layout - rotation is a no-op
    if (zones.size() == 1) {
        qCDebug(lcDbusWindow) << "Single zone layout, rotation is no-op";
        Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("single_zone"));
        return;
    }

    // Build zone order by zone number (1, 2, 3, ...)
    QVector<Zone*> orderedZones;
    for (int i = 1; i <= zones.size(); ++i) {
        for (Zone* zone : zones) {
            if (zone->zoneNumber() == i) {
                orderedZones.append(zone);
                break;
            }
        }
    }

    // If zone numbers are not sequential (shouldn't happen), fall back to vector order
    if (orderedZones.size() != zones.size()) {
        orderedZones = zones;
    }

    int numZones = orderedZones.size();

    // Build a zoneId -> index map for the current layout (normalize IDs for robust matching)
    QHash<QString, int> zoneIdToIndex;
    for (int i = 0; i < numZones; ++i) {
        zoneIdToIndex.insert(orderedZones[i]->id().toString(), i);
    }

    // Collect all non-floating snapped windows by iterating assignments directly.
    // This is more robust than iterating zones and calling getWindowsInZone(zoneId):
    // after "move one window to free space" we still see the remaining snapped window
    // even if zone ID format or layout iteration would otherwise miss it.
    struct WindowZoneInfo {
        QString windowId;
        int zoneIndex;
        QString screenName;
    };
    QVector<WindowZoneInfo> windowZoneIndices;

    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        const QString& windowId = it.key();
        const QString& zoneId = it.value();

        // Skip zone selector temporary IDs
        if (zoneId.startsWith(QStringLiteral("zoneselector-"))) {
            continue;
        }

        // Note: Floating windows are already removed from m_windowZoneAssignments when floated
        // (via windowUnsnappedForFloat), so we don't need to check m_floatingWindows here.
        // We update m_preFloatZoneAssignments separately below for floating windows whose zones rotate.

        // Resolve zone to index in current layout
        // Normalize zone ID for lookup (handle format differences)
        QString normalizedZoneId = zoneId;
        auto uuid = Utils::parseUuid(zoneId);
        if (uuid) {
            normalizedZoneId = uuid->toString(); // Normalize to default format (with braces)
        } else {
            qCWarning(lcDbusWindow) << "Invalid zone ID format in assignment for window" << windowId
                                   << "- zone ID:" << zoneId;
            // Continue with original zoneId - lookup will likely fail but we'll handle it below
        }
        auto idxIt = zoneIdToIndex.constFind(normalizedZoneId);
        if (idxIt == zoneIdToIndex.constEnd()) {
            // Zone not in active layout (e.g. orphaned after layout change)
            qCDebug(lcDbusWindow) << "Skipping window" << windowId << "- zone" << zoneId << "not in layout";
            continue;
        }

        QString screenName = m_windowScreenAssignments.value(windowId);
        windowZoneIndices.append({windowId, *idxIt, screenName});
    }

    if (windowZoneIndices.isEmpty()) {
        qCDebug(lcDbusWindow) << "No snapped windows to rotate";
        Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_snapped_windows"));
        return;
    }

    // Build rotation data: calculate destination zone for each window
    // Also build zone ID mapping for updating floating windows' pre-float zones
    QHash<QString, QString> zoneRotationMap; // oldZoneId -> newZoneId
    QJsonArray rotationArray;
    for (const auto& windowInfo : windowZoneIndices) {
        const QString& windowId = windowInfo.windowId;
        int currentIndex = windowInfo.zoneIndex;
        const QString& screenName = windowInfo.screenName;

        // Calculate destination index
        int destIndex;
        if (clockwise) {
            destIndex = (currentIndex + 1) % numZones;
        } else {
            destIndex = (currentIndex - 1 + numZones) % numZones;
        }

        Zone* currentZone = orderedZones[currentIndex];
        Zone* destZone = orderedZones[destIndex];
        QString currentZoneId = currentZone->id().toString();
        QString destZoneId = destZone->id().toString();

        // Build zone rotation map (for updating floating windows)
        zoneRotationMap.insert(currentZoneId, destZoneId);

        // Get geometry for destination zone ON THE CORRECT SCREEN
        // This fixes multi-monitor issues where windows could get wrong coordinates
        QString geometry = screenName.isEmpty() ? getZoneGeometry(destZoneId)
                                                 : getZoneGeometryForScreen(destZoneId, screenName);
        if (geometry.isEmpty()) {
            qCWarning(lcDbusWindow) << "Could not get geometry for zone" << destZone->zoneNumber();
            continue;
        }

        QJsonDocument geomDoc = QJsonDocument::fromJson(geometry.toUtf8());
        QJsonObject geomObj = geomDoc.object();

        QJsonObject moveObj;
        moveObj[QStringLiteral("windowId")] = windowId;
        moveObj[QStringLiteral("targetZoneId")] = destZoneId;
        moveObj[QStringLiteral("x")] = geomObj[QStringLiteral("x")];
        moveObj[QStringLiteral("y")] = geomObj[QStringLiteral("y")];
        moveObj[QStringLiteral("width")] = geomObj[QStringLiteral("width")];
        moveObj[QStringLiteral("height")] = geomObj[QStringLiteral("height")];
        rotationArray.append(moveObj);

        qCDebug(lcDbusWindow) << "Window" << windowId << "rotating from zone"
                              << (currentIndex + 1) << "to zone" << (destIndex + 1);
    }

    // Update pre-float zone assignments for floating windows whose zones are rotating.
    // When a floating window unfloats, it should snap to the rotated zone position.
    // Normalize zone IDs for comparison (handle format differences).
    for (auto it = m_preFloatZoneAssignments.begin(); it != m_preFloatZoneAssignments.end(); ++it) {
        const QString& oldZoneId = it.value();
        auto oldUuid = Utils::parseUuid(oldZoneId);
        if (!oldUuid) {
            qCWarning(lcDbusWindow) << "Invalid zone ID format in pre-float assignment for floating window"
                                    << it.key() << "- zone ID:" << oldZoneId;
            continue; // Skip invalid zone IDs
        }
        // Normalize to default format (with braces) for lookup
        QString normalizedOldZoneId = oldUuid->toString();
        auto rotationIt = zoneRotationMap.constFind(normalizedOldZoneId);
        if (rotationIt != zoneRotationMap.constEnd()) {
            QString newZoneId = *rotationIt;
            qCDebug(lcDbusWindow) << "Updating pre-float zone for floating window" << it.key()
                                  << "from" << oldZoneId << "to" << newZoneId;
            it.value() = newZoneId;
        }
    }

    if (rotationArray.isEmpty()) {
        qCWarning(lcDbusWindow) << "No valid rotation moves to execute";
        Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("rotation_failed"));
        return;
    }

    QString rotationData = QString::fromUtf8(QJsonDocument(rotationArray).toJson(QJsonDocument::Compact));
    qCInfo(lcDbusWindow) << "Rotating" << rotationArray.size() << "windows"
                         << (clockwise ? "clockwise" : "counterclockwise");
    Q_EMIT rotateWindowsRequested(clockwise, rotationData);
    Q_EMIT navigationFeedback(true, QStringLiteral("rotate"), QString());
}

void WindowTrackingAdaptor::reportNavigationFeedback(bool success, const QString& action, const QString& reason)
{
    // This method is called by KWin effect via D-Bus to report navigation results.
    // Emit the Qt signal which triggers the OSD display.
    qCDebug(lcDbusWindow) << "KWin effect reported navigation feedback: success=" << success << "action=" << action << "reason=" << reason;
    Q_EMIT navigationFeedback(success, action, reason);
}

void WindowTrackingAdaptor::cycleWindowsInZone(bool forward)
{
    qCDebug(lcDbusWindow) << "cycleWindowsInZone called, forward:" << forward;

    // This method cycles focus between windows stacked in the same zone as the currently
    // focused window. Useful for monocle-style workflows.
    //
    // Since we don't know which window is focused from the daemon side, we emit a signal
    // that the KWin effect will handle. The effect will:
    //   1. Get the focused window ID
    //   2. Call getZoneForWindow() to find its zone
    //   3. Call getWindowsInZone() to get all windows in that zone
    //   4. Determine the next/previous window in the list
    //   5. Activate that window
    //
    // We encode the direction as a special directive that KWin interprets.
    QString directive = forward ? QStringLiteral("cycle:forward") : QStringLiteral("cycle:backward");
    Q_EMIT cycleWindowsInZoneRequested(directive, QString());
    // Note: Success/failure feedback will be emitted by the KWin effect
}

bool WindowTrackingAdaptor::queryWindowFloating(const QString& windowId)
{
    // Query if a window is floating - called by KWin effect to sync state
    return isWindowFloating(windowId);
}

bool WindowTrackingAdaptor::isWindowFloating(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    // Use stable ID for comparison to persist floating state across KWin restarts
    QString stableId = Utils::extractStableId(windowId);
    return m_floatingWindows.contains(stableId);
}

QStringList WindowTrackingAdaptor::getFloatingWindows()
{
    return m_floatingWindows.values();
}

void WindowTrackingAdaptor::setWindowFloating(const QString& windowId, bool floating)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot set float state - empty window ID";
        return;
    }

    // Use stable ID for storage to persist floating state across KWin restarts
    // Stable ID = windowClass:resourceName (without pointer address)
    QString stableId = Utils::extractStableId(windowId);

    if (floating) {
        if (!m_floatingWindows.contains(stableId)) {
            m_floatingWindows.insert(stableId);
            qCDebug(lcDbusWindow) << "Window" << stableId << "is now floating";

            // Caller handles unsnap: effect calls windowUnsnappedForFloat before
            // setWindowFloating. Don't call windowUnsnapped here or we get
            // "Window not found for unsnap" since assignment was already removed.
            scheduleSaveState();
        }
    } else {
        if (m_floatingWindows.remove(stableId)) {
            qCDebug(lcDbusWindow) << "Window" << stableId << "is no longer floating";
            // Schedule debounced state save
            scheduleSaveState();
        }
    }
}

QString WindowTrackingAdaptor::findEmptyZone()
{
    if (!m_layoutManager) {
        qCWarning(lcDbusWindow) << "No layout manager for finding empty zone";
        return QString();
    }

    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        qCWarning(lcDbusWindow) << "No active layout for finding empty zone";
        return QString();
    }

    // Pattern B: build occupied zones from assignments, then return first layout zone not in that set.
    // Same approach as rotation — avoids relying on getWindowsInZone(zoneId) per zone.
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
            qCWarning(lcDbusWindow) << "Invalid zone ID format in assignment for window" << it.key()
                                    << "- zone ID:" << zoneId;
        }
    }

    for (int i = 0; i < layout->zoneCount(); ++i) {
        Zone* zone = layout->zone(i);
        if (!zone) {
            continue;
        }
        if (!occupiedZoneIds.contains(zone->id())) {
            QString zoneId = zone->id().toString();
            qCDebug(lcDbusWindow) << "Found empty zone" << zoneId << "at index" << i;
            return zoneId;
        }
    }

    qCDebug(lcDbusWindow) << "No empty zones found in layout";
    return QString();
}

QString WindowTrackingAdaptor::getZoneGeometry(const QString& zoneId)
{
    // Use empty screen name to fall back to primary screen
    return getZoneGeometryForScreen(zoneId, QString());
}

QString WindowTrackingAdaptor::getZoneGeometryForScreen(const QString& zoneId, const QString& screenName)
{
    if (zoneId.isEmpty()) {
        qCDebug(lcDbusWindow) << "getZoneGeometryForScreen: empty zone ID";
        return QString();
    }

    if (!m_layoutManager) {
        qCWarning(lcDbusWindow) << "getZoneGeometryForScreen: no layout manager";
        return QString();
    }

    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        qCDebug(lcDbusWindow) << "getZoneGeometryForScreen: no active layout";
        return QString();
    }

    auto zoneUuidOpt = Utils::parseUuid(zoneId);
    if (!zoneUuidOpt) {
        qCWarning(lcDbusWindow) << "getZoneGeometryForScreen: invalid UUID:" << zoneId;
        return QString();
    }

    Zone* zone = layout->zoneById(*zoneUuidOpt);
    if (!zone) {
        qCDebug(lcDbusWindow) << "getZoneGeometryForScreen: zone not found:" << zoneId;
        return QString();
    }

    // Find target screen - use specified screen name or fall back to primary
    QScreen* targetScreen = Utils::findScreenByName(screenName);
    if (!targetScreen) {
        qCWarning(lcDbusWindow) << "getZoneGeometryForScreen: screen not found:" << screenName;
        return QString();
    }

    // Recalculate zone geometry for screen
    layout->recalculateZoneGeometries(ScreenManager::actualAvailableGeometry(targetScreen));

    // Get zone geometry with gaps
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
    int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);
    QRectF zoneGeom = GeometryUtils::getZoneGeometryWithGaps(zone, targetScreen, zonePadding, outerGap, true);

    if (!zoneGeom.isValid()) {
        qCWarning(lcDbusWindow) << "getZoneGeometryForScreen: invalid geometry for zone:" << zoneId;
        return QString();
    }

    QJsonObject geomObj;
    geomObj[QStringLiteral("x")] = static_cast<int>(zoneGeom.x());
    geomObj[QStringLiteral("y")] = static_cast<int>(zoneGeom.y());
    geomObj[QStringLiteral("width")] = static_cast<int>(zoneGeom.width());
    geomObj[QStringLiteral("height")] = static_cast<int>(zoneGeom.height());

    return QString::fromUtf8(QJsonDocument(geomObj).toJson(QJsonDocument::Compact));
}

void WindowTrackingAdaptor::saveState()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup tracking = config->group(QStringLiteral("WindowTracking"));

    // Save window-zone assignments as JSON using STABLE IDs (without pointer address)
    // This allows matching windows across sessions since KWin internal IDs change on relog
    QJsonObject assignmentsObj;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        // Use stable ID as key for cross-session persistence
        QString stableId = Utils::extractStableId(it.key());
        assignmentsObj[stableId] = it.value();
    }
    // Include pending (closed-window) assignments so they are restored on reopen.
    // Only add when not already present (open-window entry wins for same stableId).
    for (auto it = m_pendingZoneAssignments.constBegin(); it != m_pendingZoneAssignments.constEnd(); ++it) {
        if (!assignmentsObj.contains(it.key())) {
            assignmentsObj[it.key()] = it.value();
        }
    }
    tracking.writeEntry(QStringLiteral("WindowZoneAssignments"),
                        QString::fromUtf8(QJsonDocument(assignmentsObj).toJson(QJsonDocument::Compact)));

    // Save window-screen assignments as JSON using STABLE IDs
    // This allows multi-monitor resolution change handling across sessions
    QJsonObject screenAssignmentsObj;
    for (auto it = m_windowScreenAssignments.constBegin(); it != m_windowScreenAssignments.constEnd(); ++it) {
        QString stableId = Utils::extractStableId(it.key());
        screenAssignmentsObj[stableId] = it.value();
    }
    tracking.writeEntry(QStringLiteral("WindowScreenAssignments"),
                        QString::fromUtf8(QJsonDocument(screenAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pending screen assignments (stableId -> screenName)
    QJsonObject pendingScreenAssignmentsObj;
    for (auto it = m_pendingZoneAssignments.constBegin(); it != m_pendingZoneAssignments.constEnd(); ++it) {
        QString screenName = m_pendingZoneScreens.value(it.key());
        if (!screenName.isEmpty()) {
            pendingScreenAssignmentsObj[it.key()] = screenName;
        }
    }
    tracking.writeEntry(QStringLiteral("PendingWindowScreenAssignments"),
                        QString::fromUtf8(QJsonDocument(pendingScreenAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pending desktop assignments (stableId -> desktop)
    QJsonObject pendingDesktopAssignmentsObj;
    for (auto it = m_pendingZoneAssignments.constBegin(); it != m_pendingZoneAssignments.constEnd(); ++it) {
        int desktop = m_pendingZoneDesktops.value(it.key(), 0);
        if (desktop > 0) {
            pendingDesktopAssignmentsObj[it.key()] = desktop;
        }
    }
    tracking.writeEntry(QStringLiteral("PendingWindowDesktopAssignments"),
                        QString::fromUtf8(QJsonDocument(pendingDesktopAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pre-snap geometries as JSON
    QJsonObject geometriesObj;
    for (auto it = m_preSnapGeometries.constBegin(); it != m_preSnapGeometries.constEnd(); ++it) {
        QJsonObject geomObj;
        geomObj[QStringLiteral("x")] = it.value().x();
        geomObj[QStringLiteral("y")] = it.value().y();
        geomObj[QStringLiteral("width")] = it.value().width();
        geomObj[QStringLiteral("height")] = it.value().height();
        geometriesObj[it.key()] = geomObj;
    }
    tracking.writeEntry(QStringLiteral("PreSnapGeometries"),
                        QString::fromUtf8(QJsonDocument(geometriesObj).toJson(QJsonDocument::Compact)));

    // Save last used zone info
    tracking.writeEntry(QStringLiteral("LastUsedZoneId"), m_lastUsedZoneId);
    tracking.writeEntry(QStringLiteral("LastUsedScreenName"), m_lastUsedScreenName);
    tracking.writeEntry(QStringLiteral("LastUsedZoneClass"), m_lastUsedZoneClass);
    tracking.writeEntry(QStringLiteral("LastUsedDesktop"), m_lastUsedDesktop);

    // Save floating windows as JSON array
    QJsonArray floatingArray;
    for (const QString& windowId : m_floatingWindows) {
        floatingArray.append(windowId);
    }
    tracking.writeEntry(QStringLiteral("FloatingWindows"),
                        QString::fromUtf8(QJsonDocument(floatingArray).toJson(QJsonDocument::Compact)));

    // Save user-snapped window classes as JSON array
    // This tracks which window classes have been explicitly snapped by the user
    // so we don't auto-snap windows that were never manually snapped
    QJsonArray userSnappedArray;
    for (const QString& windowClass : m_userSnappedClasses) {
        userSnappedArray.append(windowClass);
    }
    tracking.writeEntry(QStringLiteral("UserSnappedClasses"),
                        QString::fromUtf8(QJsonDocument(userSnappedArray).toJson(QJsonDocument::Compact)));

    config->sync();
    qCDebug(lcDbusWindow) << "Saved state -" << m_windowZoneAssignments.size() << "zone assignments,"
                          << m_windowScreenAssignments.size() << "screen assignments," << m_preSnapGeometries.size()
                          << "pre-snap geometries," << m_floatingWindows.size() << "floating windows";
}

void WindowTrackingAdaptor::loadState()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup tracking = config->group(QStringLiteral("WindowTracking"));

    // Load window-zone assignments into PENDING assignments (keyed by stable ID)
    // These will be matched against new windows by their stable ID
    // This enables restoring windows to their zones after relog even though
    // KWin internal window IDs (pointer addresses) change between sessions
    QString assignmentsJson = tracking.readEntry(QStringLiteral("WindowZoneAssignments"), QString());
    if (!assignmentsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(assignmentsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                // Store in pending assignments - will be matched by stable ID when windows appear
                m_pendingZoneAssignments[it.key()] = it.value().toString();
            }
        }
    }

    // Load pending screen assignments (stableId -> screenName)
    QString pendingScreensJson = tracking.readEntry(QStringLiteral("PendingWindowScreenAssignments"), QString());
    if (!pendingScreensJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingScreensJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString()) {
                    m_pendingZoneScreens[it.key()] = it.value().toString();
                }
            }
        }
    }

    // Load pending desktop assignments (stableId -> desktop)
    QString pendingDesktopsJson = tracking.readEntry(QStringLiteral("PendingWindowDesktopAssignments"), QString());
    if (!pendingDesktopsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingDesktopsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isDouble()) {
                    m_pendingZoneDesktops[it.key()] = it.value().toInt();
                }
            }
        }
    } else if (!m_pendingZoneAssignments.isEmpty()) {
        qCDebug(lcDbusWindow) << "No pending desktop assignments in config - clearing pending restores";
        m_pendingZoneAssignments.clear();
        m_pendingZoneScreens.clear();
    }

    if (pendingScreensJson.isEmpty() && !m_pendingZoneAssignments.isEmpty()) {
        qCDebug(lcDbusWindow) << "No pending screen assignments in config - clearing pending restores";
        m_pendingZoneAssignments.clear();
        m_pendingZoneScreens.clear();
        m_pendingZoneDesktops.clear();
    }

    if (!m_pendingZoneAssignments.isEmpty()) {
        QStringList pendingWithoutDesktop;
        for (auto it = m_pendingZoneAssignments.constBegin(); it != m_pendingZoneAssignments.constEnd(); ++it) {
            if (!m_pendingZoneDesktops.contains(it.key())) {
                pendingWithoutDesktop.append(it.key());
            }
        }
        for (const QString& stableId : pendingWithoutDesktop) {
            m_pendingZoneAssignments.remove(stableId);
            m_pendingZoneScreens.remove(stableId);
        }
        if (!pendingWithoutDesktop.isEmpty()) {
            qCDebug(lcDbusWindow) << "Removed" << pendingWithoutDesktop.size()
                                  << "pending restores without desktop assignments";
        }
    }

    // Note: Window-screen assignments are NOT loaded here because they use runtime window IDs
    // which change between sessions. The screen assignment will be re-established when
    // windows are restored to their zones via restoreToPersistedZone().

    // Load pre-snap geometries
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
                        m_preSnapGeometries[it.key()] = geom;
                    }
                }
            }
        }
    }

    // Load last used zone info
    m_lastUsedZoneId = tracking.readEntry(QStringLiteral("LastUsedZoneId"), QString());
    m_lastUsedScreenName = tracking.readEntry(QStringLiteral("LastUsedScreenName"), QString());
    m_lastUsedZoneClass = tracking.readEntry(QStringLiteral("LastUsedZoneClass"), QString());
    m_lastUsedDesktop = tracking.readEntry(QStringLiteral("LastUsedDesktop"), 0);
    if (m_lastUsedDesktop <= 0) {
        m_lastUsedZoneId.clear();
        m_lastUsedScreenName.clear();
        m_lastUsedZoneClass.clear();
    }

    // Load floating windows
    QString floatingJson = tracking.readEntry(QStringLiteral("FloatingWindows"), QString());
    if (!floatingJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(floatingJson.toUtf8());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                if (val.isString()) {
                    m_floatingWindows.insert(val.toString());
                }
            }
        }
    }

    // Load user-snapped window classes.
    // We remember which app classes have been explicitly snapped across sessions,
    // preventing auto-snap of windows that were never manually snapped.
    QString userSnappedJson = tracking.readEntry(QStringLiteral("UserSnappedClasses"), QString());
    if (!userSnappedJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(userSnappedJson.toUtf8());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                if (val.isString()) {
                    m_userSnappedClasses.insert(val.toString());
                }
            }
        }
    }

    qCDebug(lcDbusWindow) << "Loaded state -" << m_windowZoneAssignments.size() << "zone assignments,"
                          << m_preSnapGeometries.size() << "pre-snap geometries," << m_floatingWindows.size()
                          << "floating windows," << m_userSnappedClasses.size() << "user-snapped classes,"
                          << "lastUsedZone:" << m_lastUsedZoneId << "lastUsedClass:" << m_lastUsedZoneClass
                          << "lastUsedDesktop:" << m_lastUsedDesktop;
}

void WindowTrackingAdaptor::onLayoutChanged()
{
    if (!m_layoutManager) {
        return;
    }

    auto* newLayout = m_layoutManager->activeLayout();
    if (!newLayout) {
        // No active layout - clear all zone assignments
        if (!m_windowZoneAssignments.isEmpty() || !m_pendingZoneAssignments.isEmpty()) {
            qCDebug(lcDbusWindow) << "Layout cleared, removing all" << m_windowZoneAssignments.size()
                                  << "zone assignments";
            // Emit signals for all removed assignments
            for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
                Q_EMIT windowZoneChanged(it.key(), QString());
            }
            m_windowZoneAssignments.clear();
            m_windowScreenAssignments.clear();
            m_windowDesktopAssignments.clear();
            m_pendingZoneAssignments.clear();
            m_pendingZoneScreens.clear();
            m_pendingZoneDesktops.clear();
            // Clear last used zone since it no longer exists
            m_lastUsedZoneId.clear();
            m_lastUsedScreenName.clear();
            m_lastUsedZoneClass.clear();
            m_lastUsedDesktop = 0;
            saveState();
        }
        return;
    }

    // Validate existing assignments against the new layout
    // Collect windows with orphaned zone assignments (zone no longer exists)
    QStringList windowsToRemove;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        const QString& zoneId = it.value();

        // Skip zone selector special IDs - they're temporary
        if (zoneId.startsWith(QStringLiteral("zoneselector-"))) {
            continue;
        }

        auto zoneUuid = Utils::parseUuid(zoneId);
        if (!zoneUuid || !newLayout->zoneById(*zoneUuid)) {
            // Zone no longer exists in the new layout
            windowsToRemove.append(it.key());
        }
    }

    // Remove orphaned assignments
    for (const QString& windowId : windowsToRemove) {
        m_windowZoneAssignments.remove(windowId);
        m_windowScreenAssignments.remove(windowId);
        m_windowDesktopAssignments.remove(windowId);
        Q_EMIT windowZoneChanged(windowId, QString());
    }

    // Validate pending assignments (closed windows) against the new layout
    QStringList pendingToRemove;
    for (auto it = m_pendingZoneAssignments.constBegin(); it != m_pendingZoneAssignments.constEnd(); ++it) {
        const QString& zoneId = it.value();
        if (zoneId.startsWith(QStringLiteral("zoneselector-"))) {
            pendingToRemove.append(it.key());
            continue;
        }
        auto zoneUuid = Utils::parseUuid(zoneId);
        if (!zoneUuid || !newLayout->zoneById(*zoneUuid)) {
            pendingToRemove.append(it.key());
        }
    }
    for (const QString& stableId : pendingToRemove) {
        m_pendingZoneAssignments.remove(stableId);
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
    }

    // Validate last used zone still exists
    bool lastZoneCleared = false;
    if (!m_lastUsedZoneId.isEmpty()) {
        auto lastZoneUuid = Utils::parseUuid(m_lastUsedZoneId);
        if (!lastZoneUuid || !newLayout->zoneById(*lastZoneUuid)) {
            qCDebug(lcDbusWindow) << "Last used zone" << m_lastUsedZoneId << "no longer exists in new layout, clearing";
            m_lastUsedZoneId.clear();
            m_lastUsedScreenName.clear();
            m_lastUsedZoneClass.clear();
            m_lastUsedDesktop = 0;
            lastZoneCleared = true;
        }
    }

    if (!windowsToRemove.isEmpty() || !pendingToRemove.isEmpty() || lastZoneCleared) {
        scheduleSaveState();
        if (!windowsToRemove.isEmpty()) {
            qCDebug(lcDbusWindow) << "Layout changed, removed" << windowsToRemove.size() << "orphaned zone assignments";
        }
        if (!pendingToRemove.isEmpty()) {
            qCDebug(lcDbusWindow) << "Layout changed, removed" << pendingToRemove.size()
                                  << "orphaned pending assignments";
        }
    }
}

void WindowTrackingAdaptor::scheduleSaveState()
{
    // Debounce save operations - restart timer on each call
    // This batches rapid state changes into a single disk write
    if (m_saveTimer) {
        m_saveTimer->start();
    } else {
        // Fallback if timer not initialized (shouldn't happen)
        saveState();
    }
}

void WindowTrackingAdaptor::recordSnapIntent(const QString& windowId, bool wasUserInitiated)
{
    if (windowId.isEmpty()) {
        return;
    }

    if (wasUserInitiated) {
        // User explicitly snapped this window - record the class so future windows
        // of this class can be auto-snapped
        QString windowClass = Utils::extractWindowClass(windowId);
        if (!windowClass.isEmpty() && !m_userSnappedClasses.contains(windowClass)) {
            m_userSnappedClasses.insert(windowClass);
            qCDebug(lcDbusWindow) << "Recorded user snap for class" << windowClass;
            scheduleSaveState();
        }
    }
}

} // namespace PlasmaZones
