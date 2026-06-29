// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>
#include "AutotileConstants.h"
// TilingParams, TilingScreenInfo, WindowInfo, buildWindowInfos and EdgeGaps used
// to be declared inline in this header. They now live in TilingParams.h
// (per-call parameter bundles + WindowInfo builder), included here so existing
// callers that only include <PhosphorTiles/TilingAlgorithm.h> compile unchanged.
#include "TilingParams.h"

#include <QObject>
#include <QRect>
#include <QString>
#include <QSize>
#include <QVariant>
#include <QVariantMap>
#include <QVector>
#include <functional>

namespace PhosphorTiles {

class TilingState;

/**
 * @brief Abstract base class for tiling algorithms
 *
 * Each algorithm generates zone geometries based on:
 * - Number of windows to tile
 * - Screen geometry (available area)
 * - Algorithm-specific parameters (master ratio, gaps, etc.)
 *
 * Zone geometries are returned as absolute pixel coordinates matching
 * the provided screen geometry. This matches KWin's setFrameGeometry() API.
 *
 * Subclasses must implement:
 * - name(), description() for UI display
 * - calculateZones() for the actual algorithm
 *
 * Optionally override capability methods to indicate support for:
 * - Master count adjustment
 * - Split ratio adjustment
 *
 * @note Thread Safety: All algorithms are stateless — their const public
 *       methods can be called concurrently on the same instance. The
 *       TilingState parameter must not be modified during the call.
 */
class PHOSPHORTILES_EXPORT TilingAlgorithm : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString description READ description CONSTANT)
    Q_PROPERTY(bool supportsMasterCount READ supportsMasterCount CONSTANT)
    Q_PROPERTY(bool supportsSplitRatio READ supportsSplitRatio CONSTANT)

public:
    explicit TilingAlgorithm(QObject* parent = nullptr);
    ~TilingAlgorithm() override = default;

    // Prevent copying (QObject rule)
    TilingAlgorithm(const TilingAlgorithm&) = delete;
    TilingAlgorithm& operator=(const TilingAlgorithm&) = delete;

    /**
     * @brief Inject a resolver that maps an opaque instance id to its live app class.
     *
     * Used by algorithms that need per-window class info (currently only
     * LuauTileAlgorithm, which exposes it to user-authored Luau scripts).
     * Built-in geometry algorithms don't care and ignore the resolver.
     *
     * Injected by AutotileEngine::setWindowRegistry() so every algorithm
     * returned from AlgorithmRegistry::algorithm() is seeded with the live
     * registry's lookup before any lifecycle hook fires. The resolver is a
     * std::function rather than a raw WindowRegistry* so tests can plug in
     * canned answers without constructing a real registry.
     *
     * Thread safety: setter must be called from the main thread; the resolver
     * itself is invoked only from algorithm methods that already run on the
     * main thread (buildStateMap / onWindowAdded / etc.).
     */
    void setAppIdResolver(std::function<QString(const QString&)> resolver)
    {
        m_appIdResolver = std::move(resolver);
    }

    /**
     * @brief Access the current resolver. Returns a no-op (empty-string) resolver
     *        if none has been injected.
     */
    std::function<QString(const QString&)> appIdResolver() const
    {
        if (m_appIdResolver) {
            return m_appIdResolver;
        }
        return [](const QString&) {
            return QString();
        };
    }

    /**
     * @brief The id this algorithm is registered under.
     *
     * Populated by @c AlgorithmRegistry::registerAlgorithm at registration
     * time and unset when the algorithm is removed. Empty for algorithms
     * that exist but aren't currently registered (fixture stubs, transient
     * instances). Lets callers that have only a TilingAlgorithm* recover
     * the id without the O(N) reverse lookup through the registry.
     */
    QString registryId() const
    {
        return m_registryId;
    }
    /// Registry-internal setter. Not intended for direct use; @c AlgorithmRegistry
    /// calls this from registerAlgorithm / unregisterAlgorithm.
    void setRegistryId(const QString& id)
    {
        m_registryId = id;
    }

    /**
     * @brief Human-readable name of the algorithm
     * @return Algorithm name (e.g., "Master + Stack", "BSP")
     */
    virtual QString name() const = 0;

    /**
     * @brief Description of the algorithm behavior
     * @return Description suitable for tooltips/help text
     */
    virtual QString description() const = 0;

    /**
     * @brief Calculate zone geometries for N windows
     *
     * This is the core algorithm method. Given tiling parameters (window count,
     * screen geometry, state, gaps, min sizes), generate zone rectangles.
     *
     * @param params Tiling parameters (see TilingParams)
     * @return Vector of zone geometries in absolute pixel coordinates
     *
     * @note The returned vector should have exactly params.windowCount elements.
     *       For windowCount == 0, return an empty vector.
     *       For windowCount == 1, typically return a single zone inset by outerGap.
     */
    virtual QVector<QRect> calculateZones(const TilingParams& params) const = 0;

    /**
     * @brief Get the index of the "master" zone (if applicable)
     *
     * For algorithms with a master/stack concept, this returns the index
     * of the primary window zone. Used for "focus master" and "swap with master".
     *
     * @return Master zone index (0-based), or -1 if no master concept
     */
    virtual int masterZoneIndex() const;

    /**
     * @brief Check if algorithm supports variable master count
     *
     * If true, the algorithm can handle multiple windows in the master area.
     *
     * @return true if master count can be adjusted
     */
    virtual bool supportsMasterCount() const;

    /**
     * @brief Check if algorithm supports split ratio adjustment
     *
     * If true, the master/stack ratio can be dynamically adjusted.
     *
     * @return true if split ratio can be adjusted
     */
    virtual bool supportsSplitRatio() const;

    /**
     * @brief Get default split ratio for this algorithm
     *
     * Used when creating initial tiling state.
     *
     * @return Default ratio (0.0-1.0), typically 0.5-0.6
     */
    virtual qreal defaultSplitRatio() const;

    /**
     * @brief Get minimum number of windows for meaningful tiling
     *
     * Some algorithms (like Three Column) need a minimum number of windows
     * to produce a sensible layout.
     *
     * @return Minimum window count (typically 1)
     */
    virtual int minimumWindows() const;

    /**
     * @brief Get default maximum number of windows for this algorithm
     *
     * Used as the initial value of the "Max Windows" slider in the KCM,
     * and reported as the zone count on layout previews (LayoutGridDelegate,
     * zone selector). The slider resets to this value when switching algorithms.
     *
     * @return Default max window count for this algorithm (typically 4-10)
     */
    virtual int defaultMaxWindows() const;

    /**
     * @brief Whether this algorithm intentionally produces overlapping zones
     *
     * Algorithms like Cascade, Stair, and Monocle overlap zones by design.
     * When true, the post-layout enforceMinSizes pass is skipped to
     * avoid removeRectOverlaps destroying the intended layout.
     *
     * @return true if zones intentionally overlap (default: false)
     */
    virtual bool producesOverlappingZones() const;

    // ── noexcept convention for virtual methods ──────────────────────────
    // Methods below are noexcept because they only read cached POD fields.
    // Methods above (supportsMasterCount, supportsSplitRatio, etc.) are NOT
    // noexcept because LuauTileAlgorithm overrides may allocate QStrings or
    // marshal cached script values. When adding new virtuals, use noexcept only
    // if the implementation is guaranteed to never allocate or throw.

    /**
     * @brief How zone numbers should be displayed in previews
     *
     * Controls which zones show their number label in layout cards and previews.
     * Values: "all" (default), "last", "first", "firstAndLast", "none"
     *
     * @return Display mode string
     */
    virtual QString zoneNumberDisplay() const noexcept;

    /**
     * @brief Whether this algorithm uses a center layout
     *
     * Center layout algorithms (e.g., ThreeColumn, CenteredMaster) have a
     * center column whose width is controlled by the split ratio. The UI
     * labels the ratio/count controls as "Center" instead of "Master".
     *
     * @return true if this is a center layout algorithm (default: false)
     */
    virtual bool centerLayout() const;

    /**
     * @brief Whether this algorithm lays out the single-window case itself
     *
     * By default an algorithm does not own the single-window case: a scripted
     * algorithm's calculateZones() fills the work area for a lone window without
     * invoking its tile(). An algorithm that opts in (e.g. a centered-single
     * layout) receives the single-window case in its tile() computation and is
     * responsible for the resulting geometry.
     *
     * @return true if the algorithm owns the single-window layout (default: false)
     */
    virtual bool supportsSingleWindow() const noexcept;

    /**
     * @brief Whether this algorithm is a user-provided scripted algorithm
     *
     * Scripted algorithms are loaded from Luau (.luau) files at runtime.
     * Used by the UI to group algorithms into "Built-in" vs "Custom" sections.
     *
     * @return true if this is a LuauTileAlgorithm (default: false)
     */
    virtual bool isScripted() const noexcept;

    /**
     * @brief Whether this algorithm supports per-window minimum size constraints
     *
     * Most algorithms respect the minSizes parameter. Algorithms that ignore
     * it (e.g., Floating Center, Tatami) return false so the settings UI can
     * disable min-size controls for them.
     *
     * @return true if the algorithm honors minSizes (default: true)
     */
    virtual bool supportsMinSizes() const noexcept;

    /**
     * @brief Whether this algorithm maintains persistent state across retiles
     *
     * Memory algorithms (like DwindleMemory) remember per-split ratios and
     * tree structure. The UI shows an indicator for memory-enabled algorithms.
     *
     * @return true if the algorithm uses a persistent SplitTree (default: false)
     */
    virtual bool supportsMemory() const noexcept;

    /**
     * @brief Prepare the TilingState before calculateZones() is called
     *
     * Memory-based algorithms override this to lazily create their SplitTree.
     * The engine calls this unconditionally before calculateZones(), removing
     * the need for concrete algorithm casts in the engine.
     *
     * The method is const on the algorithm (it doesn't mutate algorithm state)
     * but mutates the TilingState argument — the engine owns that mutation.
     *
     * @param state TilingState to prepare (may be nullptr, implementations must check)
     */
    virtual void prepareTilingState(TilingState* state) const;

    /**
     * @brief Whether this scripted algorithm was loaded from a user directory
     *
     * System-installed scripts (shipped with Phosphor) return false.
     * User-created scripts in ~/.local/share/plasmazones/algorithms/ return true.
     * Non-scripted algorithms always return false.
     *
     * @return true if loaded from user's writable data directory (default: false)
     */
    virtual bool isUserScript() const noexcept;

    // ── Lifecycle Hooks (optional, v2) ──────────────────────────────────

    /**
     * @brief Whether this algorithm implements any lifecycle hooks
     *
     * When true, the engine calls onWindowAdded/onWindowRemoved before
     * the next calculateZones() call, giving the algorithm a chance to
     * update internal state incrementally.
     *
     * @return true if the algorithm defines lifecycle hooks (default: false)
     */
    virtual bool supportsLifecycleHooks() const noexcept;

    /**
     * @brief Called when a window is added to the tiling before retile
     *
     * Algorithms can use this to update internal state (e.g., BSP tree
     * insertion) instead of rebuilding from scratch in calculateZones().
     *
     * Unlike calculateZones() (which receives a const TilingState*),
     * lifecycle hooks receive a mutable pointer so algorithms can update
     * internal structures (e.g., split trees) incrementally.
     *
     * @param state Current tiling state (mutable for tree updates)
     * @param windowIndex Index where the window was inserted
     */
    virtual void onWindowAdded(TilingState* state, int windowIndex);

    /**
     * @brief Called when a window is removed from the tiling before retile
     *
     * The window is still present in @p state when this hook fires; it will
     * be removed immediately after the hook returns. This means
     * state->tiledWindowCount() still includes the departing window.
     * Algorithms should use @p windowIndex to identify the departing window
     * but must not assume the tiled window list will remain unchanged
     * after the call. Hooks must NOT reorder or mutate the tiled window
     * list — the engine relies on list stability for the subsequent removal.
     *
     * Unlike calculateZones() (which receives a const TilingState*),
     * lifecycle hooks receive a mutable pointer so algorithms can update
     * internal structures (e.g., split trees) incrementally.
     *
     * @param state Current tiling state (window still present, count not yet decremented)
     * @param windowIndex Index the window occupied before removal
     */
    virtual void onWindowRemoved(TilingState* state, int windowIndex);

    /**
     * @brief Whether this algorithm reacts to interactive window resizes.
     *
     * When true, the engine calls @ref onWindowResized for a non-tree algorithm
     * after the user finishes resizing a tiled window, letting the algorithm
     * record the adjustment (typically into TilingState::scriptState) before the
     * follow-up retile. Tree/memory algorithms do not use this — the engine
     * reflows their SplitTree directly. Default false.
     */
    virtual bool supportsResizeHook() const noexcept;

    /**
     * @brief Called when a tiled window finished an interactive resize.
     *
     * Only invoked for non-memory algorithms that return true from
     * @ref supportsResizeHook. The algorithm may mutate @p state (e.g. write
     * TilingState::scriptState) so the immediately-following retile lays the
     * windows out to honour the resize. Default no-op.
     *
     * @param state  Mutable tiling state (window list unchanged by the resize)
     * @param resize Which window/edges moved, with old/new frames
     */
    virtual void onWindowResized(TilingState* state, const ResizeEvent& resize);

    /**
     * @brief Whether this algorithm persists an opaque per-screen script-state
     * bag (TilingState::scriptState) across retiles and sessions.
     *
     * Scripted algorithms opt in via their metadata so the engine sanitizes and
     * round-trips the bag (e.g. an aligned grid remembering column widths).
     * Built-in algorithms do not use it. Used by the picker to surface a
     * "remembers script state" filter/indicator. Default false.
     */
    virtual bool supportsScriptState() const noexcept;

    // ── Custom Parameters (optional, v2) ──────────────────────────────────

    /**
     * @brief Whether this algorithm declares custom parameters
     *
     * Algorithms that support custom parameters (e.g., scripted algorithms
     * with @param declarations) return true. Used to avoid downcasting.
     *
     * @return true if the algorithm has custom parameter definitions (default: false)
     */
    virtual bool supportsCustomParams() const noexcept;

    /**
     * @brief Get custom parameter definitions as a QVariantList for QML
     *
     * Each entry is a QVariantMap with keys: name, type, defaultValue,
     * description, minValue, maxValue, enumOptions (as applicable).
     *
     * @return List of param definition maps, or empty if none declared
     */
    virtual QVariantList customParamDefList() const;

    /**
     * @brief Check if a named custom parameter is declared by this algorithm
     *
     * Lighter-weight alternative to customParamDefList() for filtering stale
     * params on the retile hot path — avoids QVariantList/QVariantMap allocation.
     *
     * @param name Parameter name to check
     * @return true if the algorithm declares a @param with this name
     */
    virtual bool hasCustomParam(const QString& name) const;

protected:
    // Resolver from instance id → live app class. Set by AutotileEngine via
    // setAppIdResolver after constructing/looking up the algorithm. Default
    // is an empty std::function; appIdResolver() returns a no-op lambda in
    // that case so call sites can invoke it unconditionally.
    std::function<QString(const QString&)> m_appIdResolver;

    // Registry id — populated by AlgorithmRegistry at registration time so
    // preview / serialisation code holding only a TilingAlgorithm* can
    // recover the id without walking the registry.
    QString m_registryId;

    /**
     * @brief Compute the usable area after subtracting uniform outer gap from screen edges
     *
     * @param screenGeometry Full screen rectangle
     * @param outerGap Gap at each edge in pixels
     * @return Inset rectangle (clamped to at least 1x1)
     */
    static QRect innerRect(const QRect& screenGeometry, int outerGap);

    /**
     * @brief Compute the usable area after subtracting per-side outer gaps from screen edges
     *
     * @param screenGeometry Full screen rectangle
     * @param gaps Per-side gap values
     * @return Inset rectangle (clamped to at least 1x1)
     */
    static QRect innerRect(const QRect& screenGeometry, const EdgeGaps& gaps);

Q_SIGNALS:
    /**
     * @brief Emitted when algorithm parameters change
     *
     * Connect to this signal to trigger retiling when algorithm
     * configuration is modified at runtime.
     */
    void configurationChanged();
};

} // namespace PhosphorTiles