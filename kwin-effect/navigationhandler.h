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
    void handleResnapToNewLayout(const QString& resnapData);
    void handleSnapAllWindows(const QString& snapData, const QString& screenName);
    void handleCycleWindowsInZone(const QString& directive, const QString& unused);

    // Floating window tracking
    bool isWindowFloating(const QString& stableId) const;
    void setWindowFloating(const QString& stableId, bool floating);
    void syncFloatingWindowsFromDaemon();
    void syncFloatingStateForWindow(const QString& stableId);

    // Access to floating set for effect
    const QSet<QString>& floatingWindows() const { return m_floatingWindows; }

private:
    /**
     * @brief Result from batch snap/rotate/resnap operations
     */
    struct BatchSnapResult {
        enum Status { Success, ParseError, EmptyData, DbusError };
        Status status = Success;
        int successCount = 0;
        QString firstSourceZoneId;
        QString firstTargetZoneId;
    };

    /**
     * @brief Shared core for batch snap operations (rotate, resnap, snap-all)
     *
     * Parses JSON array of snap entries, validates D-Bus interface, builds window map,
     * and applies geometry + windowSnapped for each valid entry.
     *
     * @param jsonData JSON array of {windowId, targetZoneId, sourceZoneId, x, y, width, height}
     * @param filterCurrentDesktop If true, skip windows not on current desktop/activity (resnap)
     * @param resolveFullWindowId If true, resolve full windowId via getWindowId() (resnap)
     */
    BatchSnapResult applyBatchSnapFromJson(const QString& jsonData,
                                           bool filterCurrentDesktop = false,
                                           bool resolveFullWindowId = false);

    // Internal helper for float toggle - called after daemon state is synced
    void executeFloatToggle(KWin::EffectWindow* activeWindow, const QString& windowId,
                            const QString& stableId, const QString& screenName, bool newFloatState);

    PlasmaZonesEffect* m_effect;
    QSet<QString> m_floatingWindows;
};

} // namespace PlasmaZones
