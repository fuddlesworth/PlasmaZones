// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QString>

class QDBusInterface;
class QDBusPendingCallWatcher;

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
    void handleSwapWindows(const QString& targetZoneId, const QString& targetWindowId, const QString& zoneGeometry);
    void handleRotateWindows(bool clockwise, const QString& rotationData);
    void handleResnapToNewLayout(const QString& resnapData);
    void handleSnapAllWindows(const QString& snapData, const QString& screenName);
    void handleCycleWindowsInZone(const QString& directive, const QString& unused);

    // Floating window tracking (uses full windowId, with appId fallback)
    bool isWindowFloating(const QString& windowId) const;
    void setWindowFloating(const QString& windowId, bool floating);
    void syncFloatingWindowsFromDaemon();
    void syncFloatingStateForWindow(const QString& windowId);

private:
    /**
     * @brief Result from batch snap/rotate/resnap operations
     */
    struct BatchSnapResult
    {
        enum Status {
            Success,
            ParseError,
            EmptyData,
            DbusError
        };
        Status status = Success;
        int successCount = 0;
        QString firstSourceZoneId;
        QString firstTargetZoneId;
        QString firstScreenName;
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
    BatchSnapResult applyBatchSnapFromJson(const QString& jsonData, bool filterCurrentDesktop = false,
                                           bool resolveFullWindowId = false);

    // Move-to-zone directive handlers (decomposed from handleMoveWindowToZone)
    void handleNavigateMove(KWin::EffectWindow* activeWindow, const QString& windowId, const QString& screenName,
                            const QString& direction);
    void handlePushMove(KWin::EffectWindow* activeWindow, const QString& windowId, const QString& screenName,
                        const QString& pushScreenName);
    void handleSnapByNumber(KWin::EffectWindow* activeWindow, const QString& windowId, const QString& screenName,
                            int zoneNumber, const QString& snapScreenName);
    void handleDirectZoneSnap(KWin::EffectWindow* activeWindow, const QString& windowId, const QString& screenName,
                              const QString& targetZoneId, const QString& zoneGeometry);

    // Float toggle helpers (decomposed from executeFloatToggle)
    void executeFloatToggle(KWin::EffectWindow* activeWindow, const QString& windowId, const QString& screenName,
                            bool newFloatState);
    void executeFloatOn(KWin::EffectWindow* activeWindow, const QString& windowId, const QString& screenName);
    void executeFloatOff(KWin::EffectWindow* activeWindow, const QString& windowId, const QString& screenName);

    /**
     * @brief Get valid WindowTracking interface or emit navigation feedback on failure.
     * @return Valid QDBusInterface* or nullptr (feedback already emitted)
     */
    QDBusInterface* requireInterface(const QString& action, const QString& screenName);

    /**
     * @brief Shared callback body for daemon-driven snap replies (navigate/push/snap).
     *
     * Parses the JSON reply, validates geometry, stores pre-snap geometry if needed,
     * applies snap, and calls windowSnapped + recordSnapIntent.
     */
    void applyDaemonSnapReply(QDBusPendingCallWatcher* watcher, QPointer<KWin::EffectWindow> safeWindow,
                              const QString& windowId, const QString& screenName, const QRectF& preSnapGeom,
                              const QString& action);

    /**
     * @brief Emit navigation feedback for a BatchSnapResult.
     */
    void emitBatchFeedback(const BatchSnapResult& result, const QString& action, const QString& screenName);

    PlasmaZonesEffect* m_effect;
    QSet<QString> m_floatingWindows;
};

} // namespace PlasmaZones
