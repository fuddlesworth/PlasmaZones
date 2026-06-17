// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorCompositor/FloatingCache.h>

#include <QHash>
#include <QObject>
#include <QString>

class QDBusPendingCallWatcher;

namespace PlasmaZones {

// Targeted using-declaration, not a namespace-wide directive: headers must
// not leak the whole PhosphorCompositor namespace into every includer.
using PhosphorCompositor::FloatingCache;

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
    // Keyed by the full composite windowId. A window is "snapped" iff it has a
    // non-empty zone entry (snap mode only — autotile tiles carry no zone UUID
    // and never appear here). Floating state is tracked separately above.

    /// The snap-zone UUID @p windowId occupies, or empty if it occupies none.
    QString zoneForWindow(const QString& windowId) const
    {
        return m_zoneByWindow.value(windowId);
    }
    /// True iff @p windowId is snapped into a zone (has a non-empty zone entry).
    bool isWindowSnapped(const QString& windowId) const
    {
        return !m_zoneByWindow.value(windowId).isEmpty();
    }
    /// Record @p windowId's zone. An empty @p zoneId removes the entry (the
    /// window left its zone — unsnapped / floated / screen-changed).
    void setWindowZone(const QString& windowId, const QString& zoneId)
    {
        if (zoneId.isEmpty()) {
            m_zoneByWindow.remove(windowId);
        } else {
            m_zoneByWindow.insert(windowId, zoneId);
        }
    }
    void clearWindowZone(const QString& windowId)
    {
        m_zoneByWindow.remove(windowId);
    }
    void clearAllZoneState()
    {
        m_zoneByWindow.clear();
    }

private:
    PlasmaZonesEffect* m_effect;
    FloatingCache m_floatingCache;
    QHash<QString, QString> m_zoneByWindow; ///< composite windowId → snap-zone UUID
};

} // namespace PlasmaZones
