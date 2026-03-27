// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "core/constants.h"
#include <QObject>
#include <QRect>
#include <QString>
#include <QSize>
#include <QVector>

namespace PlasmaZones {

class TilingState;

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
    const TilingState* state = nullptr; ///< Current tiling state (must be non-null)
    int innerGap = 0; ///< Gap between adjacent zones in pixels
    EdgeGaps outerGaps; ///< Gaps at screen edges in pixels (per-side)
    QVector<QSize> minSizes = {}; ///< Per-window minimum sizes (may be empty)
};

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
class PLASMAZONES_EXPORT TilingAlgorithm : public QObject
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
    virtual int masterZoneIndex() const noexcept;

    /**
     * @brief Check if algorithm supports variable master count
     *
     * If true, the algorithm can handle multiple windows in the master area.
     *
     * @return true if master count can be adjusted
     */
    virtual bool supportsMasterCount() const noexcept;

    /**
     * @brief Check if algorithm supports split ratio adjustment
     *
     * If true, the master/stack ratio can be dynamically adjusted.
     *
     * @return true if split ratio can be adjusted
     */
    virtual bool supportsSplitRatio() const noexcept;

    /**
     * @brief Get default split ratio for this algorithm
     *
     * Used when creating initial tiling state.
     *
     * @return Default ratio (0.0-1.0), typically 0.5-0.6
     */
    virtual qreal defaultSplitRatio() const noexcept;

    /**
     * @brief Get minimum number of windows for meaningful tiling
     *
     * Some algorithms (like Three Column) need a minimum number of windows
     * to produce a sensible layout.
     *
     * @return Minimum window count (typically 1)
     */
    virtual int minimumWindows() const noexcept;

    /**
     * @brief Get default maximum number of windows for this algorithm
     *
     * Used as the initial value of the "Max Windows" slider in the KCM,
     * and reported as the zone count on layout previews (LayoutGridDelegate,
     * zone selector). The slider resets to this value when switching algorithms.
     *
     * @return Default max window count for this algorithm (typically 4-10)
     */
    virtual int defaultMaxWindows() const noexcept;

    /**
     * @brief Whether this algorithm intentionally produces overlapping zones
     *
     * Algorithms like Cascade, Stair, and Monocle overlap zones by design.
     * When true, the post-layout enforceWindowMinSizes pass is skipped to
     * avoid removeZoneOverlaps destroying the intended layout.
     *
     * @return true if zones intentionally overlap (default: false)
     */
    virtual bool producesOverlappingZones() const noexcept;

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
     * @brief Whether this algorithm is a user-provided scripted algorithm
     *
     * Scripted algorithms are loaded from JavaScript files at runtime.
     * Used by the UI to group algorithms into "Built-in" vs "Custom" sections.
     *
     * @return true if this is a ScriptedAlgorithm (default: false)
     */
    virtual bool isScripted() const noexcept;

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

protected:
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

} // namespace PlasmaZones