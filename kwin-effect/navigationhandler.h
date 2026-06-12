// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorCompositor/FloatingCache.h>

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

private:
    PlasmaZonesEffect* m_effect;
    FloatingCache m_floatingCache;
};

} // namespace PlasmaZones
