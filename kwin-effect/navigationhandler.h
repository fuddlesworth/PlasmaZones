// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QRect>
#include <QSet>
#include <QHash>
#include <QDBusInterface>
#include <memory>

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

class PlasmaZonesEffect;

/**
 * @brief Handles keyboard navigation operations for PlasmaZones
 *
 * Processes keyboard navigation
 * commands (move, focus, swap, rotate, cycle, restore, float toggle).
 *
 * It delegates window lookups and geometry application back to the effect.
 */
class NavigationHandler : public QObject
{
    Q_OBJECT

public:
    explicit NavigationHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    // Navigation operations (called from effect's D-Bus signal handlers)
    void handleMoveWindowToZone(const QString& targetZoneId, const QString& zoneGeometry);
    void handleFocusWindowInZone(const QString& targetZoneId, const QString& windowId);
    void handleRestoreWindow();
    void handleToggleWindowFloat(bool shouldFloat);
    void handleSwapWindows(const QString& targetZoneId, const QString& targetWindowId,
                           const QString& zoneGeometry);
    void handleRotateWindows(bool clockwise, const QString& rotationData);
    void handleCycleWindowsInZone(const QString& directive, const QString& unused);

    // Floating window tracking
    bool isWindowFloating(const QString& stableId) const;
    void setWindowFloating(const QString& stableId, bool floating);
    void syncFloatingWindowsFromDaemon();
    void syncFloatingStateForWindow(const QString& stableId);

    // Access to floating set for effect
    const QSet<QString>& floatingWindows() const { return m_floatingWindows; }

private:
    PlasmaZonesEffect* m_effect;
    QSet<QString> m_floatingWindows;
};

} // namespace PlasmaZones
