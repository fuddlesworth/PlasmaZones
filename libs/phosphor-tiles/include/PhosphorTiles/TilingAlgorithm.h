// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>
#include "AutotileConstants.h"
// EdgeGaps is the per-side gap shape shared between manual layout and tiling;
// it lives in libs/phosphor-layout-api so neither side has to depend on the
// other's headers.
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <QObject>
#include <QRect>
#include <QString>
#include <QSize>
#include <QVariant>
#include <QVariantMap>
#include <QVector>
#include <functional>

namespace PhosphorTiles {

// Shorthand for the shared shape — declared here (rather than relying on a
// transitive include of core/constants.h) so this header is self-contained.
using EdgeGaps = ::PhosphorLayout::EdgeGaps;

class TilingState;

/**
 * @brief Per-window metadata passed to algorithms
 *
 * Provides identity and state for each tiled window so algorithms
 * can make app-aware layout decisions (e.g., "browser always master").
 */
struct WindowInfo
{
    QString appId; ///< Application identifier (e.g., "firefox", "org.kde.dolphin")
    bool focused = false; ///< Whether this window currently has focus
};

/**
 * @brief Screen metadata passed to tiling algorithms
 *
 * Provides physical screen context so algorithms can adapt to
 * monitor orientation and multi-monitor setups.
 */
struct TilingScreenInfo
{
    QString id; ///< Screen connector name (e.g., "HDMI-1", "DP-2")
    bool portrait = false; ///< True if height > width (portrait orientation)
    qreal aspectRatio = 0.0; ///< width/height (e.g., 1.78 for 16:9, 0.56 for portrait)
};

/**
 * @brief Parameters for zone calculation
 *
 * Bundles all inputs to calculateZones() into a single struct so new
 * parameters can be added without changing the virtual interface.
 */
struct TilingParams
{
    int windowCount = 0; ///< Number of windows to tile
    QRect screenGeometry; ///< Available screen area in absolute pixels
    /// Current tiling state. Must be non-null for all algorithm calls —
    /// algorithms may dereference without checking. The engine guarantees
    /// this by constructing TilingState before calling calculateZones().
    const TilingState* state = nullptr;
    int innerGap = 0; ///< Gap between adjacent zones in pixels
    EdgeGaps outerGaps; ///< Gaps at screen edges in pixels (per-side)
    QVector<QSize> minSizes = {}; ///< Per-window minimum sizes (may be empty)

    // ── Enriched context (v2) ──────────────────────────────────────────
    QVector<WindowInfo> windowInfos; ///< Per-window metadata (parallel to window list)
    int focusedIndex = -1; ///< Index of focused window in tiled list (-1 = unknown)
    TilingScreenInfo screenInfo; ///< Physical screen metadata
    QVariantMap customParams; ///< Algorithm-declared custom parameters

    /// Create minimal params for preview rendering (no per-window/screen context)
    static TilingParams forPreview(int count, const QRect& rect, const TilingState* state)
    {
        TilingParams p;
        p.windowCount = count;
        p.screenGeometry = rect;
        p.state = state;
        return p;
    }
};

/**
 * @brief Build per-window metadata from a TilingState
 *
 * Shared between AutotileEngine (for TilingParams construction) and
 * ScriptedAlgorithm (for lifecycle hook JS state). Identifies the focused
 * window; app class is resolved via the caller-supplied @p appIdResolver so
 * live class lookups hit the WindowRegistry instead of parsing stale strings.
 *
 * TilingState::m_windowOrder contains bare instance ids; parsing them as
 * "appId|uuid" would hand hex strings to user-authored JS algorithms. The
 * resolver lets each caller plug in whatever knows the live class for a
 * given instance id (typically AutotileEngine::currentAppIdFor bound as a
 * lambda, which consults the shared WindowRegistry).
 *
 * @param state         Current tiling state (may be null — returns empty vector)
 * @param windowCount   Number of windows to process (may differ from state->tiledWindowCount())
 * @param appIdResolver Function that maps an instance id to its current class.
 *                      Pass a no-op returning QString() to keep info.appId empty.
 * @param[out] focusedIndex Set to the index of the focused window, or -1
 * @return WindowInfo vector (empty if state is null; size may be less than windowCount)
 */
PHOSPHORTILES_EXPORT QVector<WindowInfo> buildWindowInfos(const TilingState* state, int windowCount,
                                                          const std::function<QString(const QString&)>& appIdResolver,
                                                          int& focusedIndex);

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
     * ScriptedAlgorithm, which exposes class to user-authored JS). Built-in
     * geometry algorithms don't care and ignore the resolver.
     *
     * Injected by AutotileEngine::setWindowRegistry() so every algorithm
     * returned from AlgorithmRegistry::algorithm() is seeded with the live
     * registry's lookup before any lifecycle hook fires. The resolver is a
     * std::function rather than a raw WindowRegistry* so tests can plug in
     * canned answers without constructing a real registry.
     *
     * Thread safety: setter must be called from the main thread; the resolver
     * itself is invoked only from algorithm methods that already run on the
     * main thread (buildJsState / onWindowAdded / etc.).
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
     * When true, the post-layout enforceWindowMinSizes pass is skipped to
     * avoid removeZoneOverlaps destroying the intended layout.
     *
     * @return true if zones intentionally overlap (default: false)
     */
    virtual bool producesOverlappingZones() const;

    // ── noexcept convention for virtual methods ──────────────────────────
    // Methods below are noexcept because they only read cached POD fields.
    // Methods above (supportsMasterCount, supportsSplitRatio, etc.) are NOT
    // noexcept because ScriptedAlgorithm overrides may allocate QStrings or
    // invoke cached JS values. When adding new virtuals, use noexcept only
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
     * @brief Whether this algorithm is a user-provided scripted algorithm
     *
     * Scripted algorithms are loaded from JavaScript files at runtime.
     * Used by the UI to group algorithms into "Built-in" vs "Custom" sections.
     *
     * @return true if this is a ScriptedAlgorithm (default: false)
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
     * System-installed scripts (shipped with PlasmaZones) return false.
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
     * @brief Distribute a total evenly among N parts with pixel-perfect remainder handling
     *
     * Helper for algorithms that need to divide screen space evenly. Distributes
     * remainder pixels to the first parts to ensure the sum equals the total exactly.
     *
     * Example: distributeEvenly(100, 3) returns {34, 33, 33}
     *
     * @param total Total pixels to distribute
     * @param count Number of parts to divide into (must be > 0)
     * @return Vector of sizes, one per part
     */
    static QVector<int> distributeEvenly(int total, int count);

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

    /**
     * @brief Distribute total space among count items with gaps between them
     *
     * Deducts (count-1) * gap from total, then distributes the remainder
     * evenly with pixel-perfect remainder handling.
     *
     * @param total Total pixels available (including space for gaps)
     * @param count Number of items to distribute among (must be > 0)
     * @param gap Gap between adjacent items in pixels
     * @return Vector of item sizes (caller positions them with gap spacing)
     */
    static QVector<int> distributeWithGaps(int total, int count, int gap);

    /**
     * @brief Distribute total space among count items with gaps, respecting per-item minimums
     *
     * Like distributeWithGaps(), but each item can have a minimum dimension. The algorithm:
     * 1. Deducts gap space: available = total - (count-1) * gap
     * 2. If all minimums fit, gives each item its minimum + an even share of surplus
     * 3. If minimums exceed available space, distributes proportionally by minimum weight
     *
     * @param total Total pixels available (including space for gaps)
     * @param count Number of items to distribute among (must be > 0)
     * @param gap Gap between adjacent items in pixels
     * @param minDims Per-item minimum dimension (items beyond vector size default to 1px)
     * @return Vector of item sizes (caller positions them with gap spacing)
     */
    static QVector<int> distributeWithMinSizes(int total, int count, int gap, const QVector<int>& minDims);

    /**
     * @brief Extract minimum width from minSizes at the given index
     * @return The minimum width (>= 0), or 0 if index is out of range
     */
    static int minWidthAt(const QVector<QSize>& minSizes, int index);

    /**
     * @brief Extract minimum height from minSizes at the given index
     * @return The minimum height (>= 0), or 0 if index is out of range
     */
    static int minHeightAt(const QVector<QSize>& minSizes, int index);

    /**
     * @brief Solve two-column/two-row dimension distribution with min-size constraints
     *
     * When both minimums fit, clamps each to its minimum.
     * When they don't fit, distributes proportionally by minimum weight.
     *
     * @param contentDim Total available dimension (width or height minus gap)
     * @param firstDim Initial first dimension (modified in place)
     * @param secondDim Initial second dimension (modified in place)
     * @param minFirst Minimum for first dimension (0 = unconstrained)
     * @param minSecond Minimum for second dimension (0 = unconstrained)
     */
    static void solveTwoPartMinSizes(int contentDim, int& firstDim, int& secondDim, int minFirst, int minSecond);

    /**
     * @brief Apply per-window minimum size constraints (used by overlapping algorithms)
     *
     * Clamps width and height upward to the minimum from minSizes[index].
     * No-op if index is out of range or mins are zero.
     *
     * @param width Current width (modified in place)
     * @param height Current height (modified in place)
     * @param minSizes Per-window minimum sizes vector
     * @param index Window index into minSizes
     */
    static void applyPerWindowMinSize(int& width, int& height, const QVector<QSize>& minSizes, int index);

    /**
     * @brief Result of solving three-column width distribution
     */
    struct ThreeColumnWidths
    {
        int leftWidth;
        int centerWidth;
        int rightWidth;
        int leftX;
        int centerX;
        int rightX;
    };

    /**
     * @brief Solve three-column width distribution with ratio and min-size constraints
     *
     * Shared by ThreeColumnAlgorithm and CenteredMasterAlgorithm. Computes
     * left/center/right widths from a center split ratio, applying MinZoneSizePx
     * floor, min-width clamping, and joint min-width proportional fallback.
     *
     * @param areaX Left edge X coordinate
     * @param contentWidth Total width minus two inter-column gaps
     * @param innerGap Gap between columns
     * @param splitRatio Center column ratio (0.0-1.0, will be clamped)
     * @param minLeftWidth Minimum width for left column (0 = unconstrained)
     * @param minCenterWidth Minimum width for center column (0 = unconstrained)
     * @param minRightWidth Minimum width for right column (0 = unconstrained)
     * @return Solved widths and X positions
     */
    static ThreeColumnWidths solveThreeColumnWidths(int areaX, int contentWidth, int innerGap, qreal splitRatio,
                                                    int minLeftWidth, int minCenterWidth, int minRightWidth);

    /**
     * @brief Result of precomputing cumulative min dimensions for alternating V/H splits
     */
    struct CumulativeMinDims
    {
        QVector<int> minW; ///< Per-window cumulative minimum width (size = windowCount + 1)
        QVector<int> minH; ///< Per-window cumulative minimum height (size = windowCount + 1)
    };

    /**
     * @brief Precompute direction-aware cumulative min dimensions for alternating splits
     *
     * Shared by Dwindle and Spiral algorithms. Both alternate V/H splits where
     * splitV = (i % 2 == 0). Accumulates along the split axis and takes max
     * for the orthogonal axis.
     */
    static CumulativeMinDims computeAlternatingCumulativeMinDims(int windowCount, const QVector<QSize>& minSizes,
                                                                 int innerGap);

    /**
     * @brief Append graceful degradation zones when remaining area is too small
     *
     * Shared by Dwindle and Spiral. Distributes leftover windows evenly within
     * the remaining rectangle. zones.last() is resized to the first sub-zone.
     */
    static void appendGracefulDegradation(QVector<QRect>& zones, const QRect& remaining, int leftover, int innerGap);

    /**
     * @brief Clamp split ratio to min/max range, or fall back to proportional split
     *
     * Shared by BSP for both H and V branches. When minFirstRatio <= maxFirstRatio,
     * clamps ratio. Otherwise distributes proportionally by minimum weight.
     */
    static qreal clampOrProportionalFallback(qreal ratio, qreal minFirstRatio, qreal maxFirstRatio, int firstDim,
                                             int secondDim);

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