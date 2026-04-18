// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "VirtualScreen.h"
#include "phosphorscreens_export.h"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QVector>

class QWindow;

namespace Phosphor::Screens {

class IConfigStore;
class IPanelSource;

/**
 * @brief Construction-time wiring for ScreenManager.
 *
 * All members default-construct to a "nothing fancy" state:
 *   - panelSource null  → ScreenManager treats panel offsets as zero,
 *     emits @ref ScreenManager::panelGeometryReady on the next event
 *     loop turn.
 *   - configStore null  → no VS subdivision support; cache stays empty.
 *   - useGeometrySensors true → create layer-shell sensor windows for
 *     real-time available-area tracking.
 *
 * Pointers are non-owning. Source/store must outlive the manager.
 *
 * Top-level (not nested in ScreenManager) so its in-class member
 * initialisers are reachable at the @ref ScreenManager constructor's
 * default-argument parse site — nested types' initialisers are not.
 */
struct ScreenManagerConfig
{
    IPanelSource* panelSource = nullptr;
    IConfigStore* configStore = nullptr;
    bool useGeometrySensors = true;
    int maxVirtualScreensPerPhysical = 8;
};

/**
 * @brief Centralized screen-topology service.
 *
 * Owns:
 *   • physical screen monitoring (QGuiApplication add/remove/geometry signals)
 *   • virtual-screen subdivision cache (synced from an injected IConfigStore)
 *   • per-screen available-geometry computation (panel offsets via an
 *     injected IPanelSource + layer-shell sensor windows)
 *   • effective-screen resolution (the unified VS-or-physical view consumed
 *     by every PhosphorScreens client)
 *
 * NOT a singleton — the lib never owns process-global state. Consumers
 * that need a process-wide ScreenManager (the PlasmaZones daemon does)
 * provide their own thin service-locator outside this class.
 */
class PHOSPHORSCREENS_EXPORT ScreenManager : public QObject
{
    Q_OBJECT
public:
    using Config = ScreenManagerConfig;

    explicit ScreenManager(ScreenManagerConfig cfg = ScreenManagerConfig{}, QObject* parent = nullptr);
    ~ScreenManager() override;

    /// Placeholder kept for API symmetry with previous in-tree class.
    /// Real wiring happens in @ref start().
    bool init();

    /**
     * @brief Begin tracking screens.
     *
     * Connects to QGuiApplication add/remove signals, attaches per-screen
     * geometry sensors (when enabled), starts the panel source, and
     * subscribes to the config store's @c changed signal for VS refresh.
     */
    void start();

    /// Tear down everything @ref start() set up. Idempotent.
    void stop();

    // ─── Physical screen queries ─────────────────────────────────────────

    QVector<QScreen*> screens() const;
    QScreen* primaryScreen() const;
    QScreen* screenByName(const QString& name) const;

    /**
     * @brief Per-screen panel-aware available geometry.
     *
     * Reads from the live cache populated by sensor windows + panel-source
     * data. Falls back to @c QScreen::availableGeometry() when the cache
     * has no entry for @p screen yet (e.g. before @ref panelGeometryReady
     * fires). Returns invalid QRect on null screen.
     *
     * Was a static helper in the pre-extraction class; lives on the
     * instance now so the cache no longer needs file-static storage.
     */
    QRect actualAvailableGeometry(QScreen* screen) const;

    /**
     * @brief Has the panel source produced its first reading?
     *
     * Components that compute initial zone geometry at startup gate on this
     * so they don't lay out windows against the unreserved screen rect.
     * Always true when no panel source is configured (zero offsets are
     * trivially "ready").
     */
    bool isPanelGeometryReady() const;

    // ─── Virtual screen management ───────────────────────────────────────

    /// Apply a single VS subdivision config. Use the @ref IConfigStore
    /// for persistent writes; this method is for the refresh path only.
    /// Production code should mutate via the store and let
    /// @ref refreshVirtualConfigs apply the delta.
    bool setVirtualScreenConfig(const QString& physicalScreenId, const VirtualScreenConfig& config);

    /// Diff @p configs against the current cache and apply the delta.
    /// Idempotent. Removals (entries in cache but absent from @p configs)
    /// are processed before additions/changes.
    void refreshVirtualConfigs(const QHash<QString, VirtualScreenConfig>& configs);

    VirtualScreenConfig virtualScreenConfig(const QString& physicalScreenId) const;
    bool hasVirtualScreens(const QString& physicalScreenId) const;

    // ─── Effective-screen queries ────────────────────────────────────────

    QStringList effectiveScreenIds() const;
    QStringList virtualScreenIdsFor(const QString& physicalScreenId) const;
    QStringList effectiveIdsForPhysical(const QString& physicalScreenId) const;

    QRect screenGeometry(const QString& screenId) const;
    QRect screenAvailableGeometry(const QString& screenId) const;

    QScreen* physicalQScreenFor(const QString& screenId) const;
    VirtualScreenDef::PhysicalEdges physicalEdgesFor(const QString& screenId) const;

    QString virtualScreenAt(const QPoint& globalPos, const QString& physicalScreenId) const;
    QString effectiveScreenAt(const QPoint& globalPos) const;

    // ─── Panel re-query control ──────────────────────────────────────────

    /// Schedule a one-shot panel re-query after @p delayMs. Forwards to
    /// the panel source's @c requestRequery. Useful after applying
    /// geometry updates so we pick up the settled panel state (e.g.
    /// after the KDE panel editor closes). If called again before the
    /// underlying timer fires, the timer restarts.
    void scheduleDelayedPanelRequery(int delayMs);

Q_SIGNALS:
    void screenAdded(QScreen* screen);
    void screenRemoved(QScreen* screen);
    void screenGeometryChanged(QScreen* screen, const QRect& geometry);
    void availableGeometryChanged(QScreen* screen, const QRect& availableGeometry);

    /// Fired once when @ref isPanelGeometryReady transitions to true.
    /// Components that need accurate panel geometry (window restoration,
    /// initial zone layout) should wait for this before performing
    /// geometry-dependent work.
    void panelGeometryReady();

    /// Fired when a delayed panel re-query (@ref scheduleDelayedPanelRequery)
    /// has settled. The daemon uses this to trigger reapply of window
    /// geometries after a short delay.
    void delayedPanelRequeryCompleted();

    /// VS topology changed (added, removed, renamed). Handlers that depend
    /// on the VS ID set listen here.
    void virtualScreensChanged(const QString& physicalScreenId);

    /// VS regions changed but the VS ID set is unchanged (swap, rotate,
    /// boundary resize). Handlers that need to move windows to follow
    /// the new VS geometry listen here; topology-aware handlers stay on
    /// @ref virtualScreensChanged.
    void virtualScreenRegionsChanged(const QString& physicalScreenId);

private Q_SLOTS:
    void onScreenAdded(QScreen* screen);
    void onScreenRemoved(QScreen* screen);
    void onScreenGeometryChanged(const QRect& geometry);

private:
    void connectScreenSignals(QScreen* screen);
    void disconnectScreenSignals(QScreen* screen);

    // Geometry sensor (layer-shell) lifecycle
    void createGeometrySensor(QScreen* screen);
    void destroyGeometrySensor(QScreen* screen);
    void onSensorGeometryChanged(QScreen* screen);

    // Reads sensor + IPanelSource offsets for @p screen, updates
    // m_availableGeometryCache, fires availableGeometryChanged on diff.
    void calculateAvailableGeometry(QScreen* screen);

    void onPanelOffsetsChanged(QScreen* screen);
    void onPanelRequeryCompleted();

    void onConfigStoreChanged();

    Config m_cfg;
    bool m_valid = false;
    bool m_running = false;
    bool m_panelGeometryReadyEmitted = false;

    QVector<QScreen*> m_trackedScreens;

    // Layer-shell sensor windows (one per screen) — track live available
    // area size from the compositor.
    QHash<QScreen*, QPointer<QWindow>> m_geometrySensors;

    // Per-screen-name available-geometry cache (was file-static in
    // pre-extraction class — instance state now to avoid singleton coupling).
    mutable QHash<QString, QRect> m_availableGeometryCache;

    // Virtual screen configurations, keyed by physical screen ID.
    QHash<QString, VirtualScreenConfig> m_virtualConfigs;

    mutable QStringList m_cachedEffectiveScreenIds;
    mutable bool m_effectiveScreenIdsDirty = true;

    mutable QHash<QString, QRect> m_virtualGeometryCache;

    void invalidateVirtualGeometryCache(const QString& physicalScreenId = {}) const;
    void rebuildVirtualGeometryCache(const QString& physicalScreenId) const;

    QString virtualScreenAtWithScreen(const QPoint& globalPos, const QString& physicalScreenId, QScreen* screen) const;
};

} // namespace Phosphor::Screens
