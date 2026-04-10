// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "virtualscreen.h"
#include <QObject>
#include <QScreen>
#include <QVector>
#include <QHash>
#include <QMap>
#include <QPointer>
#include <QTimer>

class QWindow;
class QDBusServiceWatcher;

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
 * On Wayland with the layer-shell QPA plugin, this class maintains invisible
 * "sensor" windows that the compositor automatically resizes when panels change.
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
     * @brief Resolve a screen ID (physical or virtual) to the backing QScreen*.
     *
     * Tries ScreenManager::physicalQScreenFor first, then Utils::findScreenByIdOrName,
     * then Utils::primaryScreen as final fallback.
     *
     * @param screenId Screen identifier (physical or virtual)
     * @return QScreen pointer. Falls back to the primary screen if the requested
     *         screen cannot be resolved; only returns nullptr if no screens exist at all.
     */
    static QScreen* resolvePhysicalScreen(const QString& screenId);

    /**
     * @brief Schedule a one-shot panel re-query after a delay
     *
     * Use after applying geometry updates so we pick up the settled panel state
     * (e.g. after the KDE panel editor closes). If called again before the timer
     * fires, the timer is restarted. Only one delayed re-query is pending at a time.
     * @param delayMs Delay in milliseconds before querying panels again
     */
    void scheduleDelayedPanelRequery(int delayMs);

    // ═══════════════════════════════════════════════════════════════════════════
    // Virtual Screen Management
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Set virtual screen configuration for a physical screen
     *
     * Defines how a physical screen is subdivided into virtual screens.
     * Pass an empty config to remove subdivisions.
     *
     * **Production code should NOT call this directly.** Settings is the
     * source of truth for virtual screen configurations: write to
     * Settings::setVirtualScreenConfig and let the daemon's connect to
     * Settings::virtualScreenConfigsChanged drive ScreenManager via
     * refreshVirtualConfigs(). Direct calls remain supported for unit tests
     * and the refresh path itself.
     *
     * @param physicalScreenId Stable EDID-based physical screen identifier
     * @param config Virtual screen configuration (regions + names)
     */
    bool setVirtualScreenConfig(const QString& physicalScreenId, const VirtualScreenConfig& config);

    /**
     * @brief Refresh ScreenManager's cache from an authoritative config map
     *
     * Diffs @p configs against the current cache and applies the delta via
     * setVirtualScreenConfig (which validates each entry). For physical IDs
     * present in the cache but absent in @p configs, applies an empty config
     * to tear down subdivisions. This is the canonical entry point for the
     * Settings → ScreenManager observer wiring; calling it more than once is
     * idempotent.
     *
     * @param configs Authoritative configs keyed by physical screen ID
     */
    void refreshVirtualConfigs(const QHash<QString, VirtualScreenConfig>& configs);

    /**
     * @brief Get virtual screen configuration for a physical screen
     * @param physicalScreenId Physical screen identifier
     * @return Configuration, or empty config if no subdivisions
     */
    VirtualScreenConfig virtualScreenConfig(const QString& physicalScreenId) const;

    /**
     * @brief Get all virtual screen IDs across all physical screens
     *
     * For physical screens without subdivisions, returns the physical screen ID.
     * For subdivided screens, returns all virtual screen IDs.
     */
    QStringList effectiveScreenIds() const;

    /**
     * @brief Get virtual screen IDs for a specific physical screen
     * @param physicalScreenId Physical screen identifier
     * @return Virtual screen IDs, or single-element list with physical ID if not subdivided
     */
    QStringList virtualScreenIdsFor(const QString& physicalScreenId) const;

    /**
     * @brief Get absolute geometry for a virtual or physical screen
     * @param screenId Virtual or physical screen identifier
     * @return Geometry in global compositor coordinates
     */
    QRect screenGeometry(const QString& screenId) const;

    /**
     * @brief Get available geometry (minus panels) for a virtual or physical screen
     * @param screenId Virtual or physical screen identifier
     * @return Available geometry in global compositor coordinates
     */
    QRect screenAvailableGeometry(const QString& screenId) const;

    /**
     * @brief Get the physical QScreen backing a screen ID
     * @param screenId Virtual or physical screen identifier
     * @return QScreen pointer, or nullptr if not found
     */
    QScreen* physicalQScreenFor(const QString& screenId) const;

    /**
     * @brief Get effective screen IDs for a physical screen
     *
     * If the physical screen has virtual subdivisions, returns the virtual screen IDs.
     * Otherwise returns the physical screen ID as a single-element list.
     *
     * @param physicalScreenId Physical screen identifier
     * @return List of effective screen IDs
     */
    QStringList effectiveIdsForPhysical(const QString& physicalScreenId) const;

    /**
     * @brief Get effective screen IDs with fallback to QGuiApplication screens
     *
     * Returns effectiveScreenIds() if ScreenManager is available and has screens.
     * Otherwise falls back to Utils::allScreens() identifiers.
     *
     * @return List of effective screen IDs
     */
    static QStringList effectiveScreenIdsWithFallback();

    /**
     * @brief Check if a screen ID has virtual subdivisions
     * @param physicalScreenId Physical screen identifier
     * @return true if the screen has been subdivided
     */
    bool hasVirtualScreens(const QString& physicalScreenId) const;

    /**
     * @brief Get which edges of a virtual screen are at the physical screen boundary
     *
     * Internal edges (shared with another virtual screen) should use inner gap
     * instead of outer gap to avoid double gaps at virtual screen boundaries.
     *
     * @param screenId Screen identifier (returns all-true for physical screens)
     * @return PhysicalEdges struct (true = at physical boundary, false = internal)
     */
    VirtualScreenDef::PhysicalEdges physicalEdgesFor(const QString& screenId) const;

    /**
     * @brief Find which virtual screen contains a global point
     * @param globalPos Point in global compositor coordinates
     * @param physicalScreenId Physical screen to search within
     * @return Virtual screen ID, or empty string if not found
     */
    QString virtualScreenAt(const QPoint& globalPos, const QString& physicalScreenId) const;

    /**
     * @brief Find which effective screen (virtual or physical) contains a global point
     * @param globalPos Point in global compositor coordinates
     * @return Screen ID (virtual if subdivided, physical otherwise), or empty string
     */
    QString effectiveScreenAt(const QPoint& globalPos) const;

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

    /**
     * @brief Emitted when virtual screen configuration changes
     * @param physicalScreenId Physical screen whose subdivisions changed
     */
    void virtualScreensChanged(const QString& physicalScreenId);

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

    bool m_valid = false;
    bool m_running = false;
    bool m_dbusQueryPending = false;
    bool m_panelGeometryReceived = false; // True after first panel D-Bus query completes
    QVector<QScreen*> m_trackedScreens;
    QMap<QString, ScreenPanelOffsets> m_panelOffsets; // Keyed by QScreen name

    // Persistent geometry sensor windows (one per screen)
    // These invisible layer-shell windows track available geometry
    QHash<QScreen*, QPointer<QWindow>> m_geometrySensors;

    // Delayed panel re-query (e.g. after panel editor close) to pick up settled state
    QTimer m_delayedPanelRequeryTimer;

    // Watch for org.kde.plasmashell registration to query panels on arrival
    QDBusServiceWatcher* m_plasmaShellWatcher = nullptr;

    // Virtual screen configuration per physical screen
    QHash<QString, VirtualScreenConfig> m_virtualConfigs;

    // Cached result for effectiveScreenIds() — invalidated when screens or configs change
    mutable QStringList m_cachedEffectiveScreenIds;
    mutable bool m_effectiveScreenIdsDirty = true;

    // Cached absolute geometries for virtual screens (invalidated on screen geometry change)
    mutable QHash<QString, QRect> m_virtualGeometryCache;

    void invalidateVirtualGeometryCache(const QString& physicalScreenId = {}) const;
    void rebuildVirtualGeometryCache(const QString& physicalScreenId) const;

    /**
     * @brief Find which virtual screen contains a global point (QScreen* pre-resolved)
     *
     * Same as virtualScreenAt() but avoids a redundant findScreenByIdOrName() call
     * when the caller already has the QScreen*.
     */
    QString virtualScreenAtWithScreen(const QPoint& globalPos, const QString& physicalScreenId, QScreen* screen) const;
};

} // namespace PlasmaZones
