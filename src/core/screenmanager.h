// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QScreen>
#include <QVector>
#include <QHash>
#include <QMap>
#include <QTimer>

class QWindow;

namespace PlasmaZones {

class Layout;
class OverlayService;

/**
 * @brief Centralized screen management
 *
 * Handles all screen-related operations:
 * - Screen monitoring (added/removed/changed)
 * - Geometry change notifications
 * - Available geometry tracking via persistent sensor windows
 *
 * On Wayland with LayerShellQt, this class maintains invisible "sensor"
 * windows that the compositor automatically resizes when panels change.
 * This provides instant, reactive available geometry updates without
 * any probing or polling.
 */
class PLASMAZONES_EXPORT ScreenManager : public QObject
{
    Q_OBJECT

public:
    explicit ScreenManager(QObject* parent = nullptr);
    ~ScreenManager() override;

    /**
     * @brief Initialize screen monitoring
     * @return true if successful
     */
    bool init();

    /**
     * @brief Start monitoring screens
     *
     * Creates geometry sensor windows for each screen on Wayland.
     * These windows automatically track available geometry changes.
     */
    void start();

    /**
     * @brief Stop monitoring screens
     */
    void stop();

    /**
     * @brief Get all screens
     * @return Vector of QScreen pointers
     */
    QVector<QScreen*> screens() const;

    /**
     * @brief Get primary screen
     * @return Primary screen or nullptr
     */
    QScreen* primaryScreen() const;

    /**
     * @brief Get screen by name
     * @param name Screen name
     * @return Screen or nullptr
     */
    QScreen* screenByName(const QString& name) const;

    /**
     * @brief Get actual available geometry for a screen
     *
     * Returns the usable screen area excluding panels/taskbars.
     * On Wayland, this is tracked via persistent sensor windows
     * that the compositor keeps updated automatically.
     *
     * @param screen Screen to get available geometry for
     * @return Available geometry (excluding panels)
     */
    static QRect actualAvailableGeometry(QScreen* screen);

    /**
     * @brief Check if panel geometry has been received (static for D-Bus adaptor access)
     *
     * Returns true after the first D-Bus panel query has completed.
     * Use this to check if actualAvailableGeometry() will return
     * accurate results that account for panels.
     *
     * @return true if panel geometry is known, false if not yet received or no ScreenManager instance
     */
    static bool isPanelGeometryReady();

    /**
     * @brief Get the global ScreenManager instance
     *
     * @return Pointer to the ScreenManager instance, or nullptr if not initialized
     */
    static ScreenManager* instance();

    /**
     * @brief Schedule a one-shot panel re-query after a delay
     *
     * Use after applying geometry updates so we pick up the settled panel state
     * (e.g. after the KDE panel editor closes). If called again before the timer
     * fires, the timer is restarted. Only one delayed re-query is pending at a time.
     * @param delayMs Delay in milliseconds before querying panels again
     */
    void scheduleDelayedPanelRequery(int delayMs);

Q_SIGNALS:
    /**
     * @brief Emitted when a screen is added
     * @param screen Newly added screen
     */
    void screenAdded(QScreen* screen);

    /**
     * @brief Emitted when a screen is removed
     * @param screen Removed screen
     */
    void screenRemoved(QScreen* screen);

    /**
     * @brief Emitted when screen geometry changes
     * @param screen Screen with changed geometry
     * @param geometry New geometry
     */
    void screenGeometryChanged(QScreen* screen, const QRect& geometry);

    /**
     * @brief Emitted when available geometry changes (panels added/removed/resized)
     * @param screen Screen with changed available geometry
     * @param availableGeometry New available geometry
     */
    void availableGeometryChanged(QScreen* screen, const QRect& availableGeometry);

    /**
     * @brief Emitted once when panel geometry becomes known for the first time
     *
     * This signal is emitted after the first successful D-Bus panel query.
     * Components that need accurate panel geometry (like window restoration)
     * should wait for this signal before performing geometry-dependent operations.
     */
    void panelGeometryReady();

    /**
     * @brief Emitted when the delayed panel requery (e.g. after panel editor close) has completed
     *
     * The daemon uses this to trigger reapply of window geometries after a short delay,
     * so that the geometry debounce and processPendingGeometryUpdates run first.
     */
    void delayedPanelRequeryCompleted();

private Q_SLOTS:
    void onScreenAdded(QScreen* screen);
    void onScreenRemoved(QScreen* screen);
    void onScreenGeometryChanged(const QRect& geometry);

private:
    void connectScreenSignals(QScreen* screen);
    void disconnectScreenSignals(QScreen* screen);

    // Geometry sensor window management
    void createGeometrySensor(QScreen* screen);
    void destroyGeometrySensor(QScreen* screen);
    void onSensorGeometryChanged(QScreen* screen);

    /**
     * @brief Calculate available geometry for a screen using panel offsets
     * @param screen Screen to calculate geometry for
     */
    void calculateAvailableGeometry(QScreen* screen);

    /**
     * @brief Panel offset data for a screen (from KDE Plasma D-Bus)
     */
    struct ScreenPanelOffsets
    {
        int top = 0;
        int bottom = 0;
        int left = 0;
        int right = 0;
    };

    /**
     * @brief Query KDE Plasma panels via D-Bus
     *
     * Queries the KDE Plasma shell for panel positions and sizes.
     * Results are cached in m_panelOffsets and used to calculate
     * available geometry per-screen.
     * @param fromDelayedRequery If true, emit delayedPanelRequeryCompleted() when the async query finishes
     */
    void queryKdePlasmaPanels(bool fromDelayedRequery = false);

    /**
     * @brief Schedule a debounced D-Bus panel query
     *
     * Coalesces rapid changes into a single D-Bus query after a short delay.
     */
    void scheduleDbusQuery();

    bool m_running = false;
    bool m_dbusQueryPending = false;
    bool m_panelGeometryReceived = false;  // True after first panel D-Bus query completes
    QVector<QScreen*> m_trackedScreens;
    QMap<QString, ScreenPanelOffsets> m_panelOffsets; // Keyed by QScreen name

    // Persistent geometry sensor windows (one per screen)
    // These invisible LayerShellQt windows track available geometry
    QHash<QScreen*, QWindow*> m_geometrySensors;

    // Delayed panel re-query (e.g. after panel editor close) to pick up settled state
    QTimer m_delayedPanelRequeryTimer;
};

} // namespace PlasmaZones
