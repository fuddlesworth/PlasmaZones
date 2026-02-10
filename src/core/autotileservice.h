// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QRect>
#include <QJsonArray>
#include <QTimer>
#include <QVector>

namespace PlasmaZones {

class LayoutManager;
class WindowTrackingService;
class ISettings;
class Layout;

/**
 * @brief Window assignment result from auto-tile zone regeneration
 */
struct PLASMAZONES_EXPORT WindowAssignment {
    QString windowId;
    QString zoneId;
    QRect geometry;
};

/**
 * @brief Result of an auto-tile operation
 *
 * When handled is true, the caller should apply the assignments.
 * When handled is false, the layout is not Dynamic and the caller
 * should fall through to existing snap logic.
 */
struct PLASMAZONES_EXPORT AutoTileResult {
    bool handled = false;
    QVector<WindowAssignment> assignments;
};

/**
 * @brief Auto-tiling service for dynamic zone regeneration
 *
 * Handles the runtime lifecycle of Dynamic layouts:
 * - Window opens -> regenerate zones for N+1 windows -> resnap all
 * - Window closes -> regenerate zones for N-1 windows -> resnap all
 * - Master promotion -> reorder window list -> regenerate -> resnap
 * - Master ratio change -> update layout -> regenerate -> resnap
 *
 * Separate from WindowTrackingService to maintain SRP.
 * Testable without D-Bus — pure business logic.
 */
class PLASMAZONES_EXPORT AutoTileService : public QObject
{
    Q_OBJECT

public:
    explicit AutoTileService(LayoutManager* layoutManager,
                             WindowTrackingService* windowTracking,
                             ISettings* settings,
                             QObject* parent = nullptr);
    ~AutoTileService() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // #108 — Window Lifecycle Hooks
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Handle a new window opening on a Dynamic layout
     * @param windowId Full window ID from KWin
     * @param screenName Screen where the window appeared
     * @return AutoTileResult with handled=true if Dynamic, assignments for all windows
     *
     * Synchronous: the KWin effect needs geometry immediately for the new window.
     */
    AutoTileResult handleWindowOpened(const QString& windowId, const QString& screenName);

    /**
     * @brief Handle a window closing on a Dynamic layout
     * @param windowId Full window ID
     *
     * Debounced (50ms): avoids rapid-fire regeneration when multiple windows close.
     * Emits geometriesChanged signal asynchronously.
     */
    void handleWindowClosed(const QString& windowId);

    /**
     * @brief Handle window minimize/restore on a Dynamic layout
     * @param windowId Full window ID
     * @param minimized true if window was minimized, false if restored
     */
    void handleWindowMinimized(const QString& windowId, bool minimized);

    /**
     * @brief Handle layout change for a screen
     * @param screenName Screen whose layout changed
     *
     * Immediate regeneration for the new layout.
     */
    void handleLayoutChanged(const QString& screenName);

    // ═══════════════════════════════════════════════════════════════════════════
    // #106 — Master Window
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the current master window for a screen
     * @param screenName Screen to query
     * @return Window ID of master, or empty if none
     */
    QString masterWindowId(const QString& screenName) const;

    /**
     * @brief Promote a window to master position
     * @param windowId Window to promote (swaps with current master)
     * @param screenName Screen where the window is
     *
     * Immediate: user expects instant feedback from Meta+Return.
     */
    void promoteMasterWindow(const QString& windowId, const QString& screenName);

    // ═══════════════════════════════════════════════════════════════════════════
    // #107 — Master Ratio Resize
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Adjust the master area ratio for a screen's layout
     * @param screenName Screen whose layout to adjust
     * @param delta Amount to change (e.g. +0.05 or -0.05)
     *
     * Immediate: user expects instant feedback from Meta+L/H.
     */
    void adjustMasterRatio(const QString& screenName, qreal delta);

    // ═══════════════════════════════════════════════════════════════════════════
    // Queries
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if a screen's current layout is Dynamic
     */
    bool isScreenDynamic(const QString& screenName) const;

    /**
     * @brief Get the count of tiled windows on a screen
     */
    int tiledWindowCount(const QString& screenName) const;

    /**
     * @brief Convert assignments to JSON array for D-Bus signal/response
     *
     * Public so the D-Bus adaptor can serialize partial results without
     * duplicating the JSON construction logic.
     */
    QJsonArray assignmentsToJson(const QVector<WindowAssignment>& assignments) const;

Q_SIGNALS:
    /**
     * @brief Emitted when auto-tile geometries change asynchronously
     * @param screenName Screen that was regenerated
     * @param assignments JSON array: [{"windowId":"...", "zoneId":"...", "x":N, "y":N, "w":N, "h":N}, ...]
     *
     * The KWin effect should apply these geometries to all listed windows.
     */
    void geometriesChanged(const QString& screenName, const QJsonArray& assignments);

private:
    /**
     * @brief Core regeneration: regenerate zones and compute assignments
     * @param screenName Screen to regenerate for
     * @return AutoTileResult with assignments for all tiled windows
     */
    AutoTileResult regenerateForScreen(const QString& screenName);

    /**
     * @brief Regenerate zones and emit geometriesChanged signal
     * @param screenName Screen to regenerate for
     *
     * Common pattern for user-initiated actions (promote, ratio change, layout change).
     */
    void regenerateAndEmit(const QString& screenName);

    /**
     * @brief Resolve layout for screen only if it's Dynamic
     * @param screenName Screen to query
     * @return Layout pointer if Dynamic, nullptr otherwise
     */
    Layout* resolveDynamicLayout(const QString& screenName) const;

    /**
     * @brief Get ordered list of tiled windows (master first, then by zone number)
     * @param screenName Screen to query
     * @return Ordered window IDs
     */
    QStringList orderedTiledWindows(const QString& screenName) const;

    /**
     * @brief Schedule a debounced regeneration for a screen
     * @param screenName Screen to regenerate
     */
    void scheduleRegeneration(const QString& screenName);

    /**
     * @brief Process all pending debounced regenerations
     */
    void processPendingRegenerations();

    // Dependencies
    LayoutManager* m_layoutManager;
    WindowTrackingService* m_windowTracking;
    ISettings* m_settings;

    // Per-screen master window tracking
    QHash<QString, QString> m_masterWindows; // screenName -> windowId

    // Per-screen tiled window lists (windows participating in auto-tile)
    // Tracks which windows are on each screen for Dynamic layouts
    QHash<QString, QStringList> m_tiledWindows; // screenName -> [windowId, ...]

    // Reverse lookup: windowId -> screenName
    QHash<QString, QString> m_windowScreens;

    // Minimized windows (excluded from zone count but tracked for restore)
    QSet<QString> m_minimizedWindows;

    // Debounce timer for window close/minimize
    QTimer m_debounceTimer;
    QSet<QString> m_pendingScreens;
};

} // namespace PlasmaZones
