// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorCompositor/FloatingCache.h>
#include <PhosphorCompositor/ZoneCache.h>

#include <QObject>
#include <QString>

class QDBusPendingCallWatcher;

namespace PlasmaZones {

// Targeted using-declaration, not a namespace-wide directive: headers must
// not leak the whole PhosphorCompositor namespace into every includer.
using PhosphorCompositor::FloatingCache;
using PhosphorCompositor::ZoneCache;

class PlasmaZonesEffect;

/**
 * @brief Manages floating window state cache for PlasmaZones
 *
 * Navigation operations (move, focus, swap, restore, cycle, rotate, resnap) are now
 * daemon-driven: the daemon computes geometry/targets and emits applyGeometryRequested,
 * activateWindowRequested, or applyGeometriesBatch. The effect applies them directly.
 *
 * This handler retains floating window state tracking (synced from daemon via D-Bus
 * signals, queried locally for fast checks in shouldHandleWindow/isWindowFloating).
 */
class NavigationHandler : public QObject
{
    Q_OBJECT

public:
    explicit NavigationHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    // Floating window tracking — delegates to shared FloatingCache
    bool isWindowFloating(const QString& windowId) const
    {
        return m_floatingCache.isFloating(windowId);
    }
    void setWindowFloating(const QString& windowId, bool floating)
    {
        m_floatingCache.setFloating(windowId, floating);
    }
    void clearAllFloatingState()
    {
        m_floatingCache.clear();
    }
    void syncFloatingWindowsFromDaemon();
    void syncFloatingStateForWindow(const QString& windowId);
    void syncZonesFromDaemon();

    // Snap-zone tracking — synced from the daemon's windowStateChanged signal,
    // queried by the rule-query builder for the IsSnapped / Zone match fields.
    // Delegates to the shared ZoneCache (sibling of FloatingCache), which keys by
    // the stable instanceId so a class-mutating snapped window stays resolvable.

    /// The snap-zone UUID @p windowId occupies, or empty if it occupies none.
    QString zoneForWindow(const QString& windowId) const
    {
        return m_zoneCache.zoneForWindow(windowId);
    }
    /// True iff @p windowId is snapped into a zone (snap mode only — autotile
    /// tiles carry no zone UUID and never appear here).
    bool isWindowSnapped(const QString& windowId) const
    {
        return m_zoneCache.isSnapped(windowId);
    }
    /// Record @p windowId's zone. An empty @p zoneId removes the entry (the
    /// window left its zone — unsnapped / floated / screen-changed).
    void setWindowZone(const QString& windowId, const QString& zoneId)
    {
        m_zoneCache.setZone(windowId, zoneId);
    }
    void clearWindowZone(const QString& windowId)
    {
        m_zoneCache.remove(windowId);
    }
    void clearAllZoneState()
    {
        m_zoneCache.clear();
    }
    int zoneEntryCount() const
    {
        return m_zoneCache.size();
    }

private:
    PlasmaZonesEffect* m_effect;
    FloatingCache m_floatingCache;
    ZoneCache m_zoneCache;
};

} // namespace PlasmaZones
