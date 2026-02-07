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
                                             ISettings* settings, VirtualDesktopManager* vdm,
                                             QObject* parent)
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
    //
    // Layout change handling: WindowTrackingAdaptor connects to activeLayoutChanged and calls
    // onLayoutChanged(). Do NOT connect here - duplicate invocation would clear m_resnapBuffer
    // on the second run (after assignments were already removed), causing no_windows_to_resnap.

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
    assignWindowToZones(windowId, QStringList{zoneId}, screenName, virtualDesktop);
}

void WindowTrackingService::assignWindowToZones(const QString& windowId, const QStringList& zoneIds,
                                                 const QString& screenName, int virtualDesktop)
{
    if (windowId.isEmpty() || zoneIds.isEmpty() || zoneIds.first().isEmpty()) {
        return;
    }

    // Only emit signal if value actually changed
    QStringList previousZones = m_windowZoneAssignments.value(windowId);
    bool zoneChanged = (previousZones != zoneIds);

    m_windowZoneAssignments[windowId] = zoneIds;
    m_windowScreenAssignments[windowId] = screenName;
    m_windowDesktopAssignments[windowId] = virtualDesktop;

    // NOTE: Do NOT store to m_pendingZoneAssignments here!
    // Pending assignments are for session persistence and should only be populated
    // when a window closes (in windowClosed()). Storing here causes ALL previously-snapped
    // windows to auto-restore on open, even when they shouldn't.

    if (zoneChanged) {
        Q_EMIT windowZoneChanged(windowId, zoneIds.first());
    }
    scheduleSaveState();
}

void WindowTrackingService::unassignWindow(const QString& windowId)
{
    // Get the zones before removing (needed for last-used zone check)
    QStringList previousZoneIds = m_windowZoneAssignments.take(windowId);
    if (previousZoneIds.isEmpty()) {
        return;  // Window wasn't assigned, nothing to do
    }

    m_windowScreenAssignments.remove(windowId);
    m_windowDesktopAssignments.remove(windowId);

    // Clear last-used zone if we're unsnapping from it
    // This preserves last-used zone when unsnapping a different window
    if (!m_lastUsedZoneId.isEmpty() && previousZoneIds.contains(m_lastUsedZoneId)) {
        m_lastUsedZoneId.clear();
        m_lastUsedScreenName.clear();
        m_lastUsedZoneClass.clear();
        m_lastUsedDesktop = 0;
    }

    // Don't remove from pending - keep for session restore
    // (pending is keyed by stable ID anyway)

    Q_EMIT windowZoneChanged(windowId, QString());
    scheduleSaveState();
}

QString WindowTrackingService::zoneForWindow(const QString& windowId) const
{
    const QStringList zones = m_windowZoneAssignments.value(windowId);
    return zones.isEmpty() ? QString() : zones.first();
}

QStringList WindowTrackingService::zonesForWindow(const QString& windowId) const
{
    return m_windowZoneAssignments.value(windowId);
}

QStringList WindowTrackingService::windowsInZone(const QString& zoneId) const
{
    QStringList result;
    for (auto it = m_windowZoneAssignments.constBegin();
         it != m_windowZoneAssignments.constEnd(); ++it) {
        if (it.value().contains(zoneId)) {
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
    // Use stableId for consistent matching across sessions
    // This allows pre-snap geometry to persist when windows are closed and reopened
    QString stableId = Utils::extractStableId(windowId);

    // Validate stableId - empty means malformed windowId
    if (stableId.isEmpty()) {
        qCWarning(lcCore) << "Cannot store pre-snap geometry: empty stableId from windowId" << windowId;
        return;
    }

    // Only store on FIRST snap - don't overwrite when moving A→B
    if (m_preSnapGeometries.contains(stableId)) {
        return;
    }

    if (geometry.isValid()) {
        m_preSnapGeometries[stableId] = geometry;

        // Memory cleanup: limit pre-snap geometry cache to prevent unbounded growth
        // Keep max 100 entries, removing an arbitrary entry when exceeded
        static constexpr int MaxPreSnapGeometries = 100;
        if (m_preSnapGeometries.size() > MaxPreSnapGeometries) {
            // Remove first entry (oldest in insertion order for QHash)
            auto it = m_preSnapGeometries.begin();
            if (it != m_preSnapGeometries.end()) {
                m_preSnapGeometries.erase(it);
            }
        }

        scheduleSaveState();
    }
}

std::optional<QRect> WindowTrackingService::preSnapGeometry(const QString& windowId) const
{
    // Use stableId for consistent matching across sessions
    QString stableId = Utils::extractStableId(windowId);
    if (stableId.isEmpty()) {
        return std::nullopt;
    }
    if (m_preSnapGeometries.contains(stableId)) {
        return m_preSnapGeometries.value(stableId);
    }
    return std::nullopt;
}

bool WindowTrackingService::hasPreSnapGeometry(const QString& windowId) const
{
    // Use stableId for consistent matching across sessions
    QString stableId = Utils::extractStableId(windowId);
    if (stableId.isEmpty()) {
        return false;
    }
    return m_preSnapGeometries.contains(stableId);
}

void WindowTrackingService::clearPreSnapGeometry(const QString& windowId)
{
    // Use stableId for consistent matching across sessions
    QString stableId = Utils::extractStableId(windowId);
    if (stableId.isEmpty()) {
        return;
    }
    if (m_preSnapGeometries.remove(stableId) > 0) {
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
    // Use stableId for consistent matching across sessions
    // m_floatingWindows stores stableIds for persistence
    QString stableId = Utils::extractStableId(windowId);
    return m_floatingWindows.contains(stableId);
}

void WindowTrackingService::setWindowFloating(const QString& windowId, bool floating)
{
    // Use stableId for consistent matching across sessions
    // This ensures floating state persists when windows are reopened
    QString stableId = Utils::extractStableId(windowId);
    if (floating) {
        m_floatingWindows.insert(stableId);
    } else {
        m_floatingWindows.remove(stableId);
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

    // Save zone(s) and screen for restore on unfloat
    if (m_windowZoneAssignments.contains(windowId)) {
        QStringList zoneIds = m_windowZoneAssignments.value(windowId);
        m_preFloatZoneAssignments[stableId] = zoneIds;
        // Save the screen where the window was snapped so unfloat restores to the correct monitor
        QString screenName = m_windowScreenAssignments.value(windowId);
        if (!screenName.isEmpty()) {
            m_preFloatScreenAssignments[stableId] = screenName;
        }
        qCDebug(lcCore) << "Saved pre-float zones for" << stableId << "->" << zoneIds << "screen:" << screenName;
        unassignWindow(windowId);
    }
    // Note: If window not in assignments, it's already unsnapped - no action needed
}

QString WindowTrackingService::preFloatZone(const QString& windowId) const
{
    QString stableId = Utils::extractStableId(windowId);
    const QStringList zones = m_preFloatZoneAssignments.value(stableId);
    return zones.isEmpty() ? QString() : zones.first();
}

QStringList WindowTrackingService::preFloatZones(const QString& windowId) const
{
    QString stableId = Utils::extractStableId(windowId);
    return m_preFloatZoneAssignments.value(stableId);
}

QString WindowTrackingService::preFloatScreen(const QString& windowId) const
{
    QString stableId = Utils::extractStableId(windowId);
    return m_preFloatScreenAssignments.value(stableId);
}

void WindowTrackingService::clearPreFloatZone(const QString& windowId)
{
    QString stableId = Utils::extractStableId(windowId);
    m_preFloatZoneAssignments.remove(stableId);
    m_preFloatScreenAssignments.remove(stableId);
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

SnapResult WindowTrackingService::calculateSnapToAppRule(const QString& windowId,
                                                           const QString& windowScreenName,
                                                           bool isSticky) const
{
    // Check if window was floating - floating windows should NOT be auto-snapped
    QString stableId = Utils::extractStableId(windowId);
    if (m_floatingWindows.contains(stableId)) {
        return SnapResult::noSnap();
    }

    // Check sticky window handling
    if (isSticky && m_settings) {
        auto handling = m_settings->stickyWindowHandling();
        if (handling == StickyWindowHandling::IgnoreAll) {
            return SnapResult::noSnap();
        }
    }

    // Get the layout for this screen (respects per-screen/desktop/activity assignments)
    Layout* layout = m_layoutManager ? m_layoutManager->resolveLayoutForScreen(windowScreenName) : nullptr;
    if (!layout) {
        return SnapResult::noSnap();
    }

    // Extract window class and check against app rules
    QString windowClass = Utils::extractWindowClass(windowId);
    int zoneNumber = layout->matchAppRule(windowClass);
    if (zoneNumber <= 0) {
        return SnapResult::noSnap();
    }

    // Find the zone by number
    Zone* zone = layout->zoneByNumber(zoneNumber);
    if (!zone) {
        qCDebug(lcCore) << "App rule matched zone" << zoneNumber << "for" << windowClass
                        << "but zone not found in layout";
        return SnapResult::noSnap();
    }

    // Calculate geometry
    QString zoneId = zone->id().toString();
    QString screenName = windowScreenName.isEmpty() ? QString() : windowScreenName;
    QRect geo = zoneGeometry(zoneId, screenName);
    if (!geo.isValid()) {
        return SnapResult::noSnap();
    }

    qCInfo(lcCore) << "App rule matched:" << windowClass << "-> zone" << zoneNumber
                   << "(" << zoneId << ")";

    SnapResult result;
    result.shouldSnap = true;
    result.geometry = geo;
    result.zoneId = zoneId;
    result.zoneIds = QStringList{zoneId};
    result.screenName = screenName;
    return result;
}

SnapResult WindowTrackingService::calculateSnapToLastZone(const QString& windowId,
                                                           const QString& windowScreenName,
                                                           bool isSticky) const
{
    // Check if feature is enabled
    if (!m_settings || !m_settings->moveNewWindowsToLastZone()) {
        return SnapResult::noSnap();
    }

    // Check if window was floating - floating windows should NOT be auto-snapped
    // They should remain floating when reopened
    QString stableId = Utils::extractStableId(windowId);
    if (m_floatingWindows.contains(stableId)) {
        qCDebug(lcCore) << "Window" << stableId << "was floating - skipping snap to last zone";
        return SnapResult::noSnap();
    }

    // Check sticky window handling
    if (isSticky && m_settings) {
        auto handling = m_settings->stickyWindowHandling();
        if (handling == StickyWindowHandling::IgnoreAll ||
            handling == StickyWindowHandling::RestoreOnly) {
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
    if (!windowScreenName.isEmpty() && !m_lastUsedScreenName.isEmpty() &&
        windowScreenName != m_lastUsedScreenName) {
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
    result.zoneIds = QStringList{m_lastUsedZoneId};
    result.screenName = m_lastUsedScreenName;
    return result;
}

SnapResult WindowTrackingService::calculateRestoreFromSession(const QString& windowId,
                                                               const QString& screenName,
                                                               bool isSticky) const
{
    QString stableId = Utils::extractStableId(windowId);

    // Check if window was floating - floating windows should NOT be auto-snapped
    // They should remain floating when reopened
    if (m_floatingWindows.contains(stableId)) {
        qCDebug(lcCore) << "Window" << stableId << "was floating - skipping session restore";
        return SnapResult::noSnap();
    }

    // Check sticky window handling
    if (isSticky && m_settings) {
        auto handling = m_settings->stickyWindowHandling();
        if (handling == StickyWindowHandling::IgnoreAll) {
            return SnapResult::noSnap();
        }
    }

    // Check for pending assignment
    if (!m_pendingZoneAssignments.contains(stableId)) {
        return SnapResult::noSnap();
    }

    QStringList zoneIds = m_pendingZoneAssignments.value(stableId);
    if (zoneIds.isEmpty()) {
        return SnapResult::noSnap();
    }
    QString zoneId = zoneIds.first(); // Primary zone for validation
    QString savedScreen = m_pendingZoneScreens.value(stableId, screenName);

    // BUG FIX: Verify layout context matches before restoring
    // Without this check, windows would restore even if the current layout is different
    // from the layout that was active when the window was saved

    // Check if the current layout matches the saved layout
    // Use layoutForScreen() for proper multi-screen support - each screen can have
    // a different layout assigned, so we compare against the layout for the saved screen/desktop
    QString savedLayoutId = m_pendingZoneLayouts.value(stableId);
    if (!savedLayoutId.isEmpty() && m_layoutManager) {
        int savedDesktop = m_pendingZoneDesktops.value(stableId, 0);

        // Get the layout for the saved screen/desktop context (not just activeLayout)
        Layout* currentLayout = m_layoutManager->layoutForScreen(savedScreen, savedDesktop, m_layoutManager->currentActivity());
        if (!currentLayout) {
            // Fallback to active layout if no screen-specific assignment
            currentLayout = m_layoutManager->activeLayout();
        }

        if (!currentLayout) {
            // No layout available at all - cannot validate, skip restore to be safe
            qCDebug(lcCore) << "Window" << stableId << "cannot validate layout (no current layout)"
                            << "- skipping session restore";
            return SnapResult::noSnap();
        }

        // Use QUuid comparison to avoid string format issues (with/without braces)
        QUuid savedUuid = QUuid::fromString(savedLayoutId);
        if (!savedUuid.isNull() && currentLayout->id() != savedUuid) {
            qCDebug(lcCore) << "Window" << stableId << "was saved with layout" << savedLayoutId
                            << "but current layout for screen" << savedScreen << "desktop" << savedDesktop
                            << "is" << currentLayout->id().toString()
                            << "- skipping session restore";
            return SnapResult::noSnap();
        }
    }

    // Check virtual desktop match (unless sticky or desktop 0 = all)
    // This mirrors the check in calculateSnapToLastZone() for consistency
    int savedDesktop = m_pendingZoneDesktops.value(stableId, 0);
    if (!isSticky && m_virtualDesktopManager && savedDesktop > 0) {
        int currentDesktop = m_virtualDesktopManager->currentDesktop();
        if (currentDesktop != savedDesktop) {
            qCDebug(lcCore) << "Window" << stableId << "was saved on desktop" << savedDesktop
                            << "but current desktop is" << currentDesktop
                            << "- skipping session restore";
            return SnapResult::noSnap();
        }
    }

    // Calculate geometry (use combined geometry for multi-zone)
    QRect geo;
    if (zoneIds.size() > 1) {
        geo = multiZoneGeometry(zoneIds, savedScreen);
    } else {
        geo = zoneGeometry(zoneId, savedScreen);
    }
    if (!geo.isValid()) {
        return SnapResult::noSnap();
    }

    SnapResult result;
    result.shouldSnap = true;
    result.geometry = geo;
    result.zoneId = zoneId;
    result.zoneIds = zoneIds;
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

bool WindowTrackingService::clearStalePendingAssignment(const QString& windowId)
{
    // When a user explicitly snaps a window, clear any stale pending assignment
    // from a previous session. This prevents the window from restoring to the
    // wrong zone if it's closed and reopened.
    QString stableId = Utils::extractStableId(windowId);
    bool hadPending = m_pendingZoneAssignments.remove(stableId) > 0;
    if (hadPending) {
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
        m_pendingZoneLayouts.remove(stableId);
        qCDebug(lcCore) << "Cleared stale pending assignment for" << stableId;
        scheduleSaveState();
    }
    return hadPending;
}

void WindowTrackingService::markAsAutoSnapped(const QString& windowId)
{
    if (!windowId.isEmpty()) {
        m_autoSnappedWindows.insert(windowId);
    }
}

bool WindowTrackingService::isAutoSnapped(const QString& windowId) const
{
    return m_autoSnappedWindows.contains(windowId);
}

bool WindowTrackingService::clearAutoSnapped(const QString& windowId)
{
    return m_autoSnappedWindows.remove(windowId);
}

void WindowTrackingService::consumePendingAssignment(const QString& windowId)
{
    QString stableId = Utils::extractStableId(windowId);
    if (m_pendingZoneAssignments.remove(stableId) > 0) {
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
        m_pendingZoneLayouts.remove(stableId);
        qCDebug(lcCore) << "Consumed pending assignment for" << stableId;
        scheduleSaveState();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation Helpers
// ═══════════════════════════════════════════════════════════════════════════════

QString WindowTrackingService::findEmptyZone(const QString& screenName) const
{
    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenName);
    if (!layout) {
        return QString();
    }

    // Build occupied zones from assignments (checking all zone IDs per window for multi-zone support)
    QSet<QUuid> occupiedZoneIds;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        for (const QString& zoneId : it.value()) {
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
    auto uuidOpt = Utils::parseUuid(zoneId);
    if (!uuidOpt) {
        return QRect();
    }

    // Find zone and its parent layout (search all layouts for per-screen support)
    Zone* zone = nullptr;
    Layout* layout = nullptr;
    for (Layout* l : m_layoutManager->layouts()) {
        zone = l->zoneById(*uuidOpt);
        if (zone) {
            layout = l;
            break;
        }
    }
    if (!zone) {
        return QRect();
    }

    QScreen* screen = screenName.isEmpty()
        ? Utils::primaryScreen()
        : Utils::findScreenByName(screenName);

    if (!screen) {
        screen = Utils::primaryScreen();
    }

    if (!screen) {
        return QRect();
    }

    // Use the zone's own layout for per-layout gap overrides
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
    int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);
    QRectF geoF = GeometryUtils::getZoneGeometryWithGaps(zone, screen, zonePadding, outerGap, true);

    return geoF.toRect();
}

QRect WindowTrackingService::multiZoneGeometry(const QStringList& zoneIds, const QString& screenName) const
{
    QRect combined;
    for (const QString& zoneId : zoneIds) {
        QRect geo = zoneGeometry(zoneId, screenName);
        if (geo.isValid()) {
            if (combined.isValid()) {
                combined = combined.united(geo);
            } else {
                combined = geo;
            }
        }
    }
    return combined;
}

QVector<RotationEntry> WindowTrackingService::calculateRotation(bool clockwise, const QString& screenFilter) const
{
    QVector<RotationEntry> result;

    // Group snapped windows by screen so each screen rotates independently
    // using its own per-screen layout (not the global active layout)
    QHash<QString, QVector<QPair<QString, QString>>> windowsByScreen; // screenName -> [(windowId, primaryZoneId)]
    for (auto it = m_windowZoneAssignments.constBegin();
         it != m_windowZoneAssignments.constEnd(); ++it) {
        // Skip floating windows - they should not participate in rotation
        QString stableId = Utils::extractStableId(it.key());
        if (m_floatingWindows.contains(stableId)) {
            qCDebug(lcCore) << "Window" << it.key() << "is floating - skipping rotation";
            continue;
        }

        const QStringList& zoneIdList = it.value();
        if (zoneIdList.isEmpty()) {
            continue;
        }

        QString screenName = m_windowScreenAssignments.value(it.key());

        // When a screen filter is set, only include windows on that screen
        if (!screenFilter.isEmpty() && screenName != screenFilter) {
            continue;
        }

        windowsByScreen[screenName].append({it.key(), zoneIdList.first()});
    }

    // Process each screen independently
    for (auto screenIt = windowsByScreen.constBegin();
         screenIt != windowsByScreen.constEnd(); ++screenIt) {
        const QString& screenName = screenIt.key();

        // Get the layout assigned to THIS screen (not the global active layout)
        Layout* layout = m_layoutManager->resolveLayoutForScreen(screenName);
        if (!layout || layout->zoneCount() < 2) {
            continue;
        }

        // Get zones sorted by zone number
        QVector<Zone*> zones = layout->zones();
        std::sort(zones.begin(), zones.end(), [](Zone* a, Zone* b) {
            return a->zoneNumber() < b->zoneNumber();
        });

        // Build zone ID -> index map (with and without braces for format-agnostic matching)
        QHash<QString, int> zoneIdToIndex;
        for (int i = 0; i < zones.size(); ++i) {
            QString zoneId = zones[i]->id().toString();
            zoneIdToIndex[zoneId] = i;
            QString withoutBraces = zones[i]->id().toString(QUuid::WithoutBraces);
            if (withoutBraces != zoneId) {
                zoneIdToIndex[withoutBraces] = i;
            }
        }

        // Find zone indices for windows on this screen
        QVector<QPair<QString, int>> windowZoneIndices;
        for (const auto& windowEntry : screenIt.value()) {
            const QString& windowId = windowEntry.first;
            const QString& storedZoneId = windowEntry.second;
            int zoneIndex = -1;

            // Try direct match first
            if (zoneIdToIndex.contains(storedZoneId)) {
                zoneIndex = zoneIdToIndex.value(storedZoneId);
            } else {
                // Try matching by parsing as UUID (handles format differences)
                QUuid storedUuid = QUuid::fromString(storedZoneId);
                if (!storedUuid.isNull()) {
                    for (int i = 0; i < zones.size(); ++i) {
                        if (zones[i]->id() == storedUuid) {
                            zoneIndex = i;
                            break;
                        }
                    }
                }
            }

            if (zoneIndex >= 0) {
                windowZoneIndices.append({windowId, zoneIndex});
            } else {
                qCDebug(lcCore) << "Window" << windowId << "has zone ID" << storedZoneId
                               << "not found in layout for screen" << screenName << "- skipping rotation";
            }
        }

        // Get screen and gap settings for geometry calculation
        QScreen* screen = screenName.isEmpty()
            ? Utils::primaryScreen()
            : Utils::findScreenByName(screenName);
        if (!screen) {
            screen = Utils::primaryScreen();
        }
        if (!screen) {
            continue;
        }

        int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
        int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);

        // Calculate rotated positions within this screen's zones
        for (const auto& pair : windowZoneIndices) {
            int currentIdx = pair.second;
            int targetIdx = clockwise
                ? (currentIdx + 1) % zones.size()
                : (currentIdx - 1 + zones.size()) % zones.size();

            Zone* sourceZone = zones[currentIdx];
            Zone* targetZone = zones[targetIdx];
            QRectF geoF = GeometryUtils::getZoneGeometryWithGaps(
                targetZone, screen, zonePadding, outerGap, true);
            QRect geo = geoF.toRect();

            if (geo.isValid()) {
                RotationEntry entry;
                entry.windowId = pair.first;
                entry.sourceZoneId = sourceZone->id().toString();
                entry.targetZoneId = targetZone->id().toString();
                entry.targetGeometry = geo;
                result.append(entry);
            }
        }
    }

    return result;
}

QVector<RotationEntry> WindowTrackingService::calculateResnapFromPreviousLayout()
{
    QVector<RotationEntry> result;
    if (m_resnapBuffer.isEmpty()) {
        return result;
    }

    // Group resnap entries by screen so each screen uses its own layout
    QHash<QString, QVector<const ResnapEntry*>> entriesByScreen;
    for (const ResnapEntry& entry : m_resnapBuffer) {
        entriesByScreen[entry.screenName].append(&entry);
    }

    for (auto screenIt = entriesByScreen.constBegin();
         screenIt != entriesByScreen.constEnd(); ++screenIt) {
        const QString& screenName = screenIt.key();

        // Get the layout assigned to this screen (not the global active layout)
        Layout* newLayout = m_layoutManager->resolveLayoutForScreen(screenName);
        if (!newLayout || newLayout->zoneCount() == 0) {
            continue;
        }

        QVector<Zone*> newZones = newLayout->zones();
        std::sort(newZones.begin(), newZones.end(), [](Zone* a, Zone* b) {
            return a->zoneNumber() < b->zoneNumber();
        });
        const int newZoneCount = newZones.size();

        for (const ResnapEntry* entry : screenIt.value()) {
            // Map position with cycling: 1->1, 2->2, 3->3, 4->1, 5->2 when 5->3 zones
            int targetPos = ((entry->zonePosition - 1) % newZoneCount) + 1;
            Zone* targetZone = newZones.value(targetPos - 1, nullptr);
            if (!targetZone) {
                continue;
            }

            QRect geo = zoneGeometry(targetZone->id().toString(), entry->screenName);
            if (!geo.isValid()) {
                continue;
            }

            RotationEntry rotEntry;
            rotEntry.windowId = entry->windowId;
            rotEntry.sourceZoneId = QString();
            rotEntry.targetZoneId = targetZone->id().toString();
            rotEntry.targetGeometry = geo;
            result.append(rotEntry);
        }
    }

    m_resnapBuffer.clear();
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

    for (auto it = m_windowZoneAssignments.constBegin();
         it != m_windowZoneAssignments.constEnd(); ++it) {
        QString windowId = it.key();
        const QStringList& zoneIds = it.value();
        if (zoneIds.isEmpty()) {
            continue;
        }
        QString screenName = m_windowScreenAssignments.value(windowId);

        QRect geo = (zoneIds.size() > 1) ? multiZoneGeometry(zoneIds, screenName)
                                          : zoneGeometry(zoneIds.first(), screenName);
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
    QString stableId = Utils::extractStableId(windowId);

    // Persist the zone assignment to pending BEFORE removing from active tracking.
    // This ensures the window can be restored to its zone when reopened.
    // BUT: Don't persist if the window is floating - floating windows should stay floating
    // and not be auto-snapped when reopened.
    QStringList zoneIds = m_windowZoneAssignments.value(windowId);
    QString zoneId = zoneIds.isEmpty() ? QString() : zoneIds.first();
    // Use stableId for consistent floating check (m_floatingWindows stores stableIds)
    bool isFloating = m_floatingWindows.contains(stableId);
    if (!zoneId.isEmpty() && !zoneId.startsWith(QStringLiteral("zoneselector-")) && !isFloating) {
        if (!stableId.isEmpty()) {
            m_pendingZoneAssignments[stableId] = zoneIds;

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

            // Save the layout ID to ensure we only restore if the same layout is active
            // This prevents restoring windows to wrong zones when layouts have been changed
            // Use resolveLayoutForScreen() for proper multi-screen support
            Layout* contextLayout = m_layoutManager
                ? m_layoutManager->resolveLayoutForScreen(screenName) : nullptr;
            if (contextLayout) {
                m_pendingZoneLayouts[stableId] = contextLayout->id().toString();
            } else {
                m_pendingZoneLayouts.remove(stableId);
            }

            qCDebug(lcCore) << "Persisted zone" << zoneId << "for closed window" << stableId
                            << "screen:" << screenName << "desktop:" << desktop
                            << "layout:" << (contextLayout ? contextLayout->id().toString() : QStringLiteral("none"));
        }
    }

    // Now clean up active tracking state (but NOT floating state, pre-snap geometry, or
    // pre-float zone - those are keyed by stableId and should persist across window
    // close/reopen cycles for proper session restore behavior)
    m_windowZoneAssignments.remove(windowId);
    m_windowScreenAssignments.remove(windowId);
    m_windowDesktopAssignments.remove(windowId);
    // NOTE: Don't remove from m_preSnapGeometries here - it's now keyed by stableId and should
    // persist so floating after reopen restores to the original pre-snap size
    // NOTE: Don't remove from m_floatingWindows here - it's keyed by stableId and should
    // persist so the window stays floating when reopened
    m_windowStickyStates.remove(windowId);
    m_autoSnappedWindows.remove(windowId);

    // NOTE: Keep m_preFloatZoneAssignments for the stableId so unfloating after reopen works
    // m_preFloatZoneAssignments.remove(stableId);  // REMOVED - preserves pre-float zone

    scheduleSaveState();
}

void WindowTrackingService::onLayoutChanged()
{
    // Validate zone assignments against new layout
    Layout* newLayout = m_layoutManager->activeLayout();
    if (!newLayout) {
        m_resnapBuffer.clear();
        return;
    }

    // Collect valid zone IDs from new active layout (for quick checks)
    QSet<QString> activeLayoutZoneIds;
    for (Zone* zone : newLayout->zones()) {
        activeLayoutZoneIds.insert(zone->id().toString());
    }

    // Before removing stale assignments, capture (window, zonePosition) for resnap-to-new-layout.
    // When user presses the shortcut, we map zone N -> zone N (with cycling when layout has fewer zones).
    // Include BOTH m_windowZoneAssignments (tracked) AND m_pendingZoneAssignments (session-restored
    // windows that KWin placed in zones before we got windowSnapped - e.g. after login).
    //
    // LayoutManager ensures prevLayout is never null (captures current as previous on first set).
    // When prevLayout != newLayout: capture assignments to OLD layout (real switch).
    // When prevLayout == newLayout: capture assignments to CURRENT layout (startup re-apply).
    //
    // Only replace m_resnapBuffer when we capture at least one window. If user does A->B->C (snapped
    // on A, B has no windows), prev=B yields nothing - we keep the buffer from A->B so resnap on C works.
    Layout* prevLayout = m_layoutManager->previousLayout();
    const bool layoutSwitched = (prevLayout != newLayout);
    {
        QVector<ResnapEntry> newBuffer;
        QVector<Zone*> prevZones = prevLayout->zones();
        std::sort(prevZones.begin(), prevZones.end(), [](Zone* a, Zone* b) {
            return a->zoneNumber() < b->zoneNumber();
        });
        QHash<QString, int> zoneIdToPosition; // zoneId -> 1-based position
        for (int i = 0; i < prevZones.size(); ++i) {
            zoneIdToPosition[prevZones[i]->id().toString()] = i + 1;
            QString withoutBraces = prevZones[i]->id().toString(QUuid::WithoutBraces);
            if (withoutBraces != prevZones[i]->id().toString()) {
                zoneIdToPosition[withoutBraces] = i + 1;
            }
        }
        QSet<QString> addedStableIds; // avoid duplicates when window is in both live and pending

        auto addToBuffer = [&](const QString& windowIdOrStableId, const QStringList& zoneIdList,
                               const QString& screenName, int vd) {
            QString stableId = Utils::extractStableId(windowIdOrStableId);
            if (stableId.isEmpty() || m_floatingWindows.contains(stableId) || addedStableIds.contains(stableId)) {
                return;
            }
            // Use primary zone for position mapping
            QString zoneId = zoneIdList.isEmpty() ? QString() : zoneIdList.first();
            int pos = zoneIdToPosition.value(zoneId, 0);
            if (pos <= 0) {
                // Handle zoneselector synthetic IDs: "zoneselector-{layoutId}-{index}"
                if (zoneId.startsWith(QStringLiteral("zoneselector-"))) {
                    int lastDash = zoneId.lastIndexOf(QStringLiteral("-"));
                    if (lastDash > 0) {
                        bool ok = false;
                        int idx = zoneId.mid(lastDash + 1).toInt(&ok);
                        if (ok && idx >= 0 && idx < prevZones.size()) {
                            pos = idx + 1; // 1-based position
                        }
                    }
                }
            }
            if (pos <= 0) {
                return;
            }
            addedStableIds.insert(stableId);
            ResnapEntry entry;
            entry.windowId = stableId; // KWin effect's buildWindowMap keys by stableId
            entry.zonePosition = pos;
            entry.screenName = screenName;
            entry.virtualDesktop = vd;
            newBuffer.append(entry);
        };

        const QUuid prevLayoutId = prevLayout->id();

        // Helper to check if a window's primary zone is valid in the active layout
        auto primaryZoneInActiveLayout = [&](const QStringList& zoneIdList) {
            if (zoneIdList.isEmpty()) return false;
            return activeLayoutZoneIds.contains(zoneIdList.first());
        };

        // Helper: is a window on a screen that uses the global active layout?
        // Windows on screens with per-screen assignments that differ from the
        // new active layout are unaffected by this layout change.
        auto isAffectedByGlobalChange = [&](const QString& windowScreen) -> bool {
            if (windowScreen.isEmpty()) return true;
            Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
            return !effectiveLayout || effectiveLayout == newLayout;
        };

        if (layoutSwitched) {
            // User switched layouts: capture assignments to zones from the OLD layout (not in new)
            // 1. Live assignments (windows we've tracked via windowSnapped)
            for (auto it = m_windowZoneAssignments.constBegin();
                 it != m_windowZoneAssignments.constEnd(); ++it) {
                // Skip windows on screens with per-screen layouts unaffected by this change
                QString windowScreen = m_windowScreenAssignments.value(it.key());
                if (!isAffectedByGlobalChange(windowScreen)) {
                    continue;
                }
                if (primaryZoneInActiveLayout(it.value())) {
                    continue;
                }
                addToBuffer(it.key(), it.value(), windowScreen,
                            m_windowDesktopAssignments.value(it.key(), 0));
            }

            // 2. Pending assignments (session-restored windows)
            for (auto it = m_pendingZoneAssignments.constBegin();
                 it != m_pendingZoneAssignments.constEnd(); ++it) {
                QString screenName = m_pendingZoneScreens.value(it.key());
                if (!isAffectedByGlobalChange(screenName)) {
                    continue;
                }
                if (primaryZoneInActiveLayout(it.value())) {
                    continue;
                }
                QString savedLayoutId = m_pendingZoneLayouts.value(it.key());
                if (!savedLayoutId.isEmpty()) {
                    auto savedUuid = Utils::parseUuid(savedLayoutId);
                    if (!savedUuid || *savedUuid != prevLayoutId) {
                        continue; // pending is for a different layout
                    }
                }
                int vd = m_pendingZoneDesktops.value(it.key(), 0);
                addToBuffer(it.key(), it.value(), screenName, vd);
            }
        } else {
            // Same layout (startup): capture assignments that belong to the current layout.
            // This lets resnap re-apply zone geometries for restored/pending windows.
            // 1. Live assignments in current layout
            for (auto it = m_windowZoneAssignments.constBegin();
                 it != m_windowZoneAssignments.constEnd(); ++it) {
                if (!primaryZoneInActiveLayout(it.value())) {
                    continue;
                }
                addToBuffer(it.key(), it.value(),
                            m_windowScreenAssignments.value(it.key()),
                            m_windowDesktopAssignments.value(it.key(), 0));
            }

            // 2. Pending assignments for current layout
            for (auto it = m_pendingZoneAssignments.constBegin();
                 it != m_pendingZoneAssignments.constEnd(); ++it) {
                if (!primaryZoneInActiveLayout(it.value())) {
                    continue;
                }
                QString savedLayoutId = m_pendingZoneLayouts.value(it.key());
                if (!savedLayoutId.isEmpty()) {
                    auto savedUuid = Utils::parseUuid(savedLayoutId);
                    if (!savedUuid || *savedUuid != prevLayoutId) {
                        continue;
                    }
                }
                QString screenName = m_pendingZoneScreens.value(it.key());
                int vd = m_pendingZoneDesktops.value(it.key(), 0);
                addToBuffer(it.key(), it.value(), screenName, vd);
            }
        }

        if (!newBuffer.isEmpty()) {
            m_resnapBuffer = std::move(newBuffer);
            qCInfo(lcCore) << "Resnap buffer:" << m_resnapBuffer.size()
                          << "windows (zone position -> window)";
            for (const ResnapEntry& e : m_resnapBuffer) {
                qCInfo(lcCore) << "  Zone" << e.zonePosition << "<-" << e.windowId;
            }
        }
    }

    // Remove stale assignments: check each window against its screen's effective layout
    // (not just the global active), so per-screen assignments aren't incorrectly purged
    QStringList toRemove;
    for (auto it = m_windowZoneAssignments.constBegin();
         it != m_windowZoneAssignments.constEnd(); ++it) {
        const QStringList& zoneIdList = it.value();
        if (zoneIdList.isEmpty()) {
            toRemove.append(it.key());
            continue;
        }
        QString windowScreen = m_windowScreenAssignments.value(it.key());
        Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
        if (!effectiveLayout) {
            toRemove.append(it.key());
            continue;
        }
        bool zoneFound = false;
        for (Zone* z : effectiveLayout->zones()) {
            if (z->id().toString() == zoneIdList.first()) {
                zoneFound = true;
                break;
            }
        }
        if (!zoneFound) {
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

void WindowTrackingService::setLastUsedZone(const QString& zoneId, const QString& screenName,
                                             const QString& zoneClass, int desktop)
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
        if (intersection.width() >= MinVisibleWidth &&
            intersection.height() >= MinVisibleHeight) {
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
    auto uuidOpt = Utils::parseUuid(zoneId);
    if (!uuidOpt) {
        return nullptr;
    }

    // Search all layouts, not just the active one, to support per-screen layouts
    for (Layout* layout : m_layoutManager->layouts()) {
        Zone* zone = layout->zoneById(*uuidOpt);
        if (zone) {
            return zone;
        }
    }
    return nullptr;
}

} // namespace PlasmaZones
