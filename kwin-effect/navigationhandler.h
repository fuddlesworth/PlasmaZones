// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QSet>
#include <QString>

class QDBusPendingCallWatcher;

namespace PlasmaZones {

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

    // Floating window tracking (uses full windowId, with appId fallback)
    bool isWindowFloating(const QString& windowId) const;
    void setWindowFloating(const QString& windowId, bool floating);
    void clearAllFloatingState()
    {
        m_floatingWindows.clear();
    }
    void syncFloatingWindowsFromDaemon();
    void syncFloatingStateForWindow(const QString& windowId);

private:
    PlasmaZonesEffect* m_effect;
    QSet<QString> m_floatingWindows;
};

} // namespace PlasmaZones
