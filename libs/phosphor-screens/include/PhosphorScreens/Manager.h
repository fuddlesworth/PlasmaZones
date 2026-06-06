// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "PhosphorScreens/PhysicalScreen.h"
#include "VirtualScreen.h"
#include "phosphorscreensruntime_export.h"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVector>

class QScreen;
class QWindow;

namespace PhosphorScreens {

class IConfigStore;
class IPanelSource;
class IScreenProvider;

/**
 * @brief Construction-time wiring for ScreenManager.
 *
 * All members default-construct to a "nothing fancy" state:
 *   - screenProvider null → ScreenManager builds its own QtScreenProvider
 *     (the live QGuiApplication-backed source). Inject a FakeScreenProvider
 *     to drive the add/remove/move/resize sequence from a test.
 *   - panelSource null  → ScreenManager treats panel offsets as zero,
 *     emits @ref ScreenManager::panelGeometryReady on the next event
 *     loop turn.
 *   - configStore null  → no VS subdivision support; cache stays empty.
 *   - useGeometrySensors true → create layer-shell sensor windows for
 *     real-time available-area tracking.
 *
 * Pointers are non-owning EXCEPT a null @ref screenProvider, where the
 * manager owns the QtScreenProvider it constructs. An injected source /
 * store / provider must outlive the manager.
 *
 * Top-level (not nested in ScreenManager) so its in-class member
 * initialisers are reachable at the @ref ScreenManager constructor's
 * default-argument parse site — nested types' initialisers are not.
 */
struct ScreenManagerConfig
{
    IScreenProvider* screenProvider = nullptr;
    IPanelSource* panelSource = nullptr;
    IConfigStore* configStore = nullptr;
    bool useGeometrySensors = true;
    int maxVirtualScreensPerPhysical = 8;
};

/**
 * @brief Centralized screen-topology service.
 *
 * Owns:
 *   • physical screen monitoring (via an injected IScreenProvider —
 *     add/remove/geometry lifecycle, decoupled from QGuiApplication)
 *   • virtual-screen subdivision cache (synced from an injected IConfigStore)
 *   • per-screen available-geometry computation (panel offsets via an
 *     injected IPanelSource + layer-shell sensor windows)
 *   • effective-screen resolution (the unified VS-or-physical view consumed
 *     by every PhosphorScreens client)
 *
 * NOT a singleton — the lib never owns process-global state. Consumers
 * that need a process-wide ScreenManager (the Phosphor daemon does)
 * provide their own thin service-locator outside this class.
 */
class PHOSPHORSCREENSRUNTIME_EXPORT ScreenManager : public QObject
{
    Q_OBJECT
public:
    using Config = ScreenManagerConfig;

    explicit ScreenManager(ScreenManagerConfig cfg = ScreenManagerConfig{}, QObject* parent = nullptr);
    ~ScreenManager() override;

    /**
     * @brief Begin tracking screens.
     *
     * Connects to the screen provider's add/remove/geometry signals,
     * snapshots the current output set, attaches per-screen geometry
     * sensors (when enabled), starts the panel source, and subscribes to
     * the config store's @c changed signal for VS refresh.
     */
    void start();

    /// Tear down everything @ref start() set up. Idempotent.
    void stop();

    // ─── Physical screen queries ─────────────────────────────────────────
    //
    // screens() / primaryScreen() / screenByName() reflect the screen
    // provider's LIVE output set. physicalScreenFor() and the
    // screenGeometry() / screenAvailableGeometry() resolvers below instead
    // read the TRACKED snapshot refreshed on each lifecycle signal — the two
    // agree except transiently, inside a lifecycle slot mid-resync. Prefer
    // the tracked resolvers for geometry work so a query stays consistent
    // with the available-geometry cache.

    QVector<PhysicalScreen> screens() const;
    PhysicalScreen primaryScreen() const;
    PhysicalScreen screenByName(const QString& name) const;

    /// Maximum number of virtual screens per physical monitor this manager
    /// will admit. Mirrors @ref ScreenManagerConfig::maxVirtualScreensPerPhysical
    /// — exposed so callers (D-Bus adaptor, KCM validator) can pre-validate
    /// incoming configs before handing them to @ref setVirtualScreenConfig
    /// and surface a specific rejection reason on cap overflow.
    int maxVirtualScreensPerPhysical() const
    {
        return m_cfg.maxVirtualScreensPerPhysical;
    }

    /**
     * @brief Per-screen panel-aware available geometry.
     *
     * Reads from the live cache populated by sensor windows + panel-source
     * data. Falls back to @c QScreen::availableGeometry() (or the full
     * screen rect for a synthetic screen with no QScreen) when the cache
     * has no entry for @p screen yet (e.g. before @ref panelGeometryReady
     * fires). Returns an invalid QRect on an invalid PhysicalScreen.
     *
     * Was a static helper in the pre-extraction class; lives on the
     * instance now so the cache no longer needs file-static storage.
     */
    QRect actualAvailableGeometry(const PhysicalScreen& screen) const;

    /**
     * @brief Bridge overload for consumers that still hold a live @c QScreen*.
     *
     * Resolves @p screen to a tracked @ref PhysicalScreen and delegates to
     * the value-typed overload. Falls back to @c QScreen::availableGeometry()
     * when the connector is not in the tracked set (e.g. a hotplug race),
     * and to an invalid QRect when @p screen is null — so the caller never
     * sees an empty rect for a screen Qt still considers live.
     *
     * The single conversion point for the @c QScreen* → @ref PhysicalScreen
     * available-geometry path: prefer it over an ad-hoc
     * @c actualAvailableGeometry(screenByName(...)) at the call site.
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

    QRect screenGeometry(const QString& screenId) const;
    QRect screenAvailableGeometry(const QString& screenId) const;

    /// The physical output a (physical or virtual) screen ID resolves to.
    /// Returns an invalid PhysicalScreen when nothing tracked matches.
    PhysicalScreen physicalScreenFor(const QString& screenId) const;
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

    // ─── Compositor-reported available geometry ──────────────────────────

    /**
     * @brief Record the authoritative per-screen available geometry as
     *        reported by the compositor.
     *
     * The KWin effect queries @c KWin::clientArea(MaximizeArea) — the exact
     * panel-excluded work area the compositor itself reserves for maximized
     * windows, with correct per-edge strut attribution and correct
     * auto-hide handling — and pushes it here over D-Bus.
     *
     * This is the highest-priority source in @ref calculateAvailableGeometry:
     * when an entry exists for a screen it fully determines that screen's
     * available rect, bypassing the layer-shell sensor + plasmashell D-Bus
     * heuristic. That heuristic can only *guess* which edge a strut belongs
     * to, and guesses wrong (attributes everything to the bottom edge) for
     * top-panel layouts when the plasmashell D-Bus query returns no panels.
     *
     * Pass an invalid or empty rect to clear the override for @p screenName
     * and fall back to the heuristic. Keyed by connector name. A redundant
     * call (same rect already recorded) is a no-op.
     */
    void setCompositorAvailableGeometry(const QString& screenName, const QRect& available);

Q_SIGNALS:
    void screenAdded(const PhysicalScreen& screen);
    void screenRemoved(const PhysicalScreen& screen);
    void screenGeometryChanged(const PhysicalScreen& screen);
    void availableGeometryChanged(const PhysicalScreen& screen, const QRect& availableGeometry);

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

    /// Emitted when a connected screen's identifier flips — e.g. a bare
    /// EDID ID promotes to "base/CONNECTOR" form because a second same-
    /// model monitor joined, or vice versa on removal. The manager has
    /// already re-keyed its own internal @c m_virtualConfigs by the time
    /// this fires; external consumers (SettingsConfigStore-backed
    /// persistence) must re-key their own stores under @p newId so the
    /// next @ref IConfigStore::loadAll agrees with the manager's cache.
    /// Both IDs are physical (no @c /vs:N suffix).
    void screenIdentifierChanged(const QString& oldId, const QString& newId);

private Q_SLOTS:
    void onProviderScreenAdded(const PhysicalScreen& screen);
    void onProviderScreenRemoved(const PhysicalScreen& screen);
    void onProviderScreenGeometryChanged(const PhysicalScreen& screen);

private:
    // Rebuild m_trackedScreens from the provider's current output set.
    // The provider is the single source of truth — every lifecycle slot
    // resyncs through this rather than mutating the vector incrementally.
    void syncTrackedScreens();

    // Tracked-screen lookups. Return a copy of the matching tracked screen,
    // or an invalid PhysicalScreen when nothing matches. By value rather
    // than a pointer-into-m_trackedScreens so a caller cannot dangle it
    // across a syncTrackedScreens that re-assigns the vector.
    PhysicalScreen trackedScreenByName(const QString& name) const;
    PhysicalScreen trackedScreenFor(const QString& screenId) const;

    // Geometry sensor (layer-shell) lifecycle. Keyed by connector name.
    void createGeometrySensor(const PhysicalScreen& screen);
    void destroyGeometrySensor(const QString& screenName);
    void onSensorGeometryChanged(const QString& screenName);

    // Reads sensor + IPanelSource offsets for @p screen, updates
    // m_availableGeometryCache, fires availableGeometryChanged on diff.
    void calculateAvailableGeometry(const PhysicalScreen& screen);

    void onPanelOffsetsChanged(QScreen* screen);
    void onPanelRequeryCompleted();

    void onConfigStoreChanged();

    Config m_cfg;

    // The effective screen provider — either the injected one or a
    // QtScreenProvider the manager constructed (parented to `this`) when
    // the config left it null.
    IScreenProvider* m_screenProvider = nullptr;

    bool m_running = false;
    bool m_panelGeometryReadyEmitted = false;

    QVector<PhysicalScreen> m_trackedScreens;

    // Layer-shell sensor windows (one per screen, keyed by connector name)
    // — track live available area size from the compositor.
    QHash<QString, QPointer<QWindow>> m_geometrySensors;

    // Per-screen-name available-geometry cache (was file-static in
    // pre-extraction class — instance state now to avoid singleton coupling).
    //
    // NOTE on the `mutable` accessor caches below (this one, the
    // effective-ID cache, and the virtual-geometry cache): they're
    // populated lazily by nominally-const accessors and not protected by
    // any synchronisation. The class contract is GUI-thread-only (see
    // class-level doc and ScreenIdentity.h for the same constraint) —
    // callers on worker threads will race these caches. Don't add locks
    // here without also re-auditing every const accessor to make sure
    // the mutation isn't leaked via returned QString&/QRect& references.
    mutable QHash<QString, QRect> m_availableGeometryCache;

    // Authoritative per-screen available geometry pushed by the compositor
    // bridge (the KWin effect's clientArea/MaximizeArea query). Keyed by
    // connector name. When an entry exists for a screen it overrides the
    // sensor/D-Bus heuristic entirely in calculateAvailableGeometry — see
    // setCompositorAvailableGeometry. Cleared per-screen on screen removal
    // (destroyGeometrySensor) and wholesale on stop().
    QHash<QString, QRect> m_compositorAvailableGeometry;

    // Virtual screen configurations, keyed by physical screen ID.
    QHash<QString, VirtualScreenConfig> m_virtualConfigs;

    mutable QStringList m_cachedEffectiveScreenIds;
    mutable bool m_effectiveScreenIdsDirty = true;

    mutable QHash<QString, QRect> m_virtualGeometryCache;

    // Warn-once set for screenGeometry() virtual-screen cache misses. A stale
    // "physId/vs:N" id that survives a VS-config change gets queried
    // repeatedly (per cursor move, per snap commit) — warn once per id rather
    // than flooding the journal. Pruned by invalidateVirtualGeometryCache and
    // cleared on a successful resolve so a genuinely-new miss after a
    // reconfigure still surfaces.
    mutable QSet<QString> m_warnedVirtualGeometryMisses;

    void invalidateVirtualGeometryCache(const QString& physicalScreenId = {}) const;
    void rebuildVirtualGeometryCache(const QString& physicalScreenId) const;

    QString virtualScreenAtWithScreen(const QPoint& globalPos, const QString& physicalScreenId,
                                      const PhysicalScreen& screen) const;

    /// Detect identifier flips between @p oldIds (connector name → prior
    /// identifier) and the current tracked identifiers, re-key
    /// @c m_virtualConfigs in place, and emit @ref screenIdentifierChanged
    /// so external stores can migrate too. Called from the add/remove
    /// slots after @ref syncTrackedScreens has refreshed the tracked set.
    void propagateIdentifierDrift(const QHash<QString, QString>& oldIds);
};

} // namespace PhosphorScreens
