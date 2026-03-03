// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowtrackingservice.h"
#include "constants.h"
#include "interfaces.h"
#include "layout.h"
#include "zone.h"
#include "layoutmanager.h"
#include "virtualdesktopmanager.h"
#include "utils.h"
#include "logging.h"
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
    if (windowId.isEmpty()) {
        qCWarning(lcCore) << "Cannot store pre-snap geometry: empty windowId";
        return;
    }

    // Only store on FIRST snap - don't overwrite when moving A→B
    // Use full windowId so each window instance gets its own pre-snap geometry
    // (stableId would collide for multiple instances of the same app)
    if (m_preSnapGeometries.contains(windowId)) {
        return;
    }
    // Also skip if a stableId entry exists (session-restored geometry should be preserved)
    QString stableId = Utils::extractStableId(windowId);
    if (stableId != windowId && m_preSnapGeometries.contains(stableId)) {
        return;
    }

    if (geometry.isValid()) {
        m_preSnapGeometries[windowId] = geometry;

        // Memory cleanup: limit pre-snap geometry cache to prevent unbounded growth
        // Keep max 100 entries, removing an arbitrary entry when exceeded
        static constexpr int MaxPreSnapGeometries = 100;
        if (m_preSnapGeometries.size() > MaxPreSnapGeometries) {
            // Remove an arbitrary entry (QHash iteration order is unspecified)
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
    if (windowId.isEmpty()) {
        return std::nullopt;
    }
    // Try full window ID first (runtime - distinguishes multiple instances)
    if (m_preSnapGeometries.contains(windowId)) {
        return m_preSnapGeometries.value(windowId);
    }
    // Fall back to stable ID (session restore - pointer addresses change across restarts)
    QString stableId = Utils::extractStableId(windowId);
    if (stableId != windowId && m_preSnapGeometries.contains(stableId)) {
        return m_preSnapGeometries.value(stableId);
    }
    return std::nullopt;
}

bool WindowTrackingService::hasPreSnapGeometry(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return false;
    }
    // Try full window ID first, fall back to stable ID for session-restored entries
    if (m_preSnapGeometries.contains(windowId)) {
        return true;
    }
    QString stableId = Utils::extractStableId(windowId);
    return (stableId != windowId && m_preSnapGeometries.contains(stableId));
}

void WindowTrackingService::clearPreSnapGeometry(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    bool removed = m_preSnapGeometries.remove(windowId) > 0;
    // Also remove stable ID entry (session-restored entries)
    QString stableId = Utils::extractStableId(windowId);
    if (stableId != windowId) {
        removed |= (m_preSnapGeometries.remove(stableId) > 0);
    }
    if (removed) {
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

void WindowTrackingService::storePreAutotileGeometry(const QString& windowId, const QRect& geometry)
{
    if (windowId.isEmpty() || !geometry.isValid()) {
        return;
    }
    m_preAutotileGeometries[windowId] = geometry;
    QString stableId = Utils::extractStableId(windowId);
    if (stableId != windowId) {
        m_preAutotileGeometries[stableId] = geometry;
    }
    scheduleSaveState();
}

void WindowTrackingService::clearPreAutotileGeometry(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    bool removed = m_preAutotileGeometries.remove(windowId) > 0;
    QString stableId = Utils::extractStableId(windowId);
    if (stableId != windowId) {
        removed |= (m_preAutotileGeometries.remove(stableId) > 0);
    }
    if (removed) {
        scheduleSaveState();
    }
}

std::optional<QRect> WindowTrackingService::validatedPreSnapOrAutotileGeometry(const QString& windowId) const
{
    auto geo = validatedPreSnapGeometry(windowId);
    if (geo) {
        return geo;
    }
    return validatedPreAutotileGeometry(windowId);
}

std::optional<QRect> WindowTrackingService::validatedPreAutotileGeometry(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return std::nullopt;
    }
    QRect rect;
    if (m_preAutotileGeometries.contains(windowId)) {
        rect = m_preAutotileGeometries.value(windowId);
    } else {
        QString stableId = Utils::extractStableId(windowId);
        if (stableId != windowId && m_preAutotileGeometries.contains(stableId)) {
            rect = m_preAutotileGeometries.value(stableId);
        } else {
            return std::nullopt;
        }
    }
    if (!rect.isValid() || rect.width() <= 0 || rect.height() <= 0) {
        return std::nullopt;
    }
    if (isGeometryOnScreen(rect)) {
        return rect;
    }
    return adjustGeometryToScreen(rect);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Floating Window State
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingService::isWindowFloating(const QString& windowId) const
{
    // Try full window ID first (runtime - distinguishes multiple instances)
    if (m_floatingWindows.contains(windowId)) {
        return true;
    }
    // Fall back to stable ID (session restore - pointer addresses change across restarts)
    QString stableId = Utils::extractStableId(windowId);
    return (stableId != windowId && m_floatingWindows.contains(stableId));
}

void WindowTrackingService::setWindowFloating(const QString& windowId, bool floating)
{
    // Use full windowId so each window instance has independent floating state
    // (stableId would collide for multiple instances of the same app)
    if (floating) {
        m_floatingWindows.insert(windowId);
    } else {
        m_floatingWindows.remove(windowId);
        // Also remove stable ID entry (session-restored entries)
        QString stableId = Utils::extractStableId(windowId);
        if (stableId != windowId) {
            m_floatingWindows.remove(stableId);
        }
    }
    scheduleSaveState();
}

QStringList WindowTrackingService::floatingWindows() const
{
    return m_floatingWindows.values();
}

void WindowTrackingService::unsnapForFloat(const QString& windowId)
{
    // Save zone(s) and screen for restore on unfloat.
    // Key by full windowId (not stableId) so multiple instances of the same
    // application each remember their own zone independently.
    if (m_windowZoneAssignments.contains(windowId)) {
        QStringList zoneIds = m_windowZoneAssignments.value(windowId);
        m_preFloatZoneAssignments[windowId] = zoneIds;
        // Save the screen where the window was snapped so unfloat restores to the correct monitor
        QString screenName = m_windowScreenAssignments.value(windowId);
        if (!screenName.isEmpty()) {
            m_preFloatScreenAssignments[windowId] = screenName;
        }
        qCInfo(lcCore) << "Saved pre-float zones for" << windowId << "->" << zoneIds << "screen:" << screenName;
        unassignWindow(windowId);
    }
    // Note: If window not in assignments, it's already unsnapped - no action needed
}

QString WindowTrackingService::preFloatZone(const QString& windowId) const
{
    // Try full window ID first (runtime - distinguishes multiple instances)
    QStringList zones = m_preFloatZoneAssignments.value(windowId);
    if (zones.isEmpty()) {
        // Fall back to stable ID (session restore - pointer addresses change across restarts)
        QString stableId = Utils::extractStableId(windowId);
        zones = m_preFloatZoneAssignments.value(stableId);
    }
    return zones.isEmpty() ? QString() : zones.first();
}

QStringList WindowTrackingService::preFloatZones(const QString& windowId) const
{
    // Try full window ID first, fall back to stable ID for session restore
    QStringList zones = m_preFloatZoneAssignments.value(windowId);
    if (zones.isEmpty()) {
        QString stableId = Utils::extractStableId(windowId);
        zones = m_preFloatZoneAssignments.value(stableId);
    }
    return zones;
}

QString WindowTrackingService::preFloatScreen(const QString& windowId) const
{
    // Try full window ID first, fall back to stable ID for session restore
    QString screen = m_preFloatScreenAssignments.value(windowId);
    if (screen.isEmpty()) {
        QString stableId = Utils::extractStableId(windowId);
        screen = m_preFloatScreenAssignments.value(stableId);
    }
    return screen;
}

void WindowTrackingService::clearPreFloatZone(const QString& windowId)
{
    // Remove by full window ID (runtime entries)
    m_preFloatZoneAssignments.remove(windowId);
    m_preFloatScreenAssignments.remove(windowId);
    // Also remove by stable ID (session-restored entries)
    QString stableId = Utils::extractStableId(windowId);
    if (stableId != windowId) {
        m_preFloatZoneAssignments.remove(stableId);
        m_preFloatScreenAssignments.remove(stableId);
    }
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


} // namespace PlasmaZones
