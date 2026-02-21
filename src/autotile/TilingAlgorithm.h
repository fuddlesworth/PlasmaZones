// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
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
struct TilingParams {
    int windowCount = 0;                ///< Number of windows to tile
    QRect screenGeometry;               ///< Available screen area in absolute pixels
    const TilingState *state = nullptr; ///< Current tiling state (must be non-null)
    int innerGap = 0;                   ///< Gap between adjacent zones in pixels
    int outerGap = 0;                   ///< Gap at screen edges in pixels
    QVector<QSize> minSizes = {};        ///< Per-window minimum sizes (may be empty)
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
 * - name(), description(), icon() for UI display
 * - calculateZones() for the actual algorithm
 *
 * Optionally override capability methods to indicate support for:
 * - Master count adjustment
 * - Split ratio adjustment
 *
 * @note Thread Safety: Most algorithms are stateless and their const public
 *       methods can be called concurrently. However, algorithms that maintain
 *       mutable internal state (e.g., BSPAlgorithm's persistent tree) are
 *       NOT safe for concurrent calculateZones() calls on the same instance.
 *       The AutotileEngine calls algorithms from a single thread, so this is
 *       safe in practice. The TilingState parameter must not be modified
 *       during the call.
 */
class PLASMAZONES_EXPORT TilingAlgorithm : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString description READ description CONSTANT)
    Q_PROPERTY(QString icon READ icon CONSTANT)
    Q_PROPERTY(bool supportsMasterCount READ supportsMasterCount CONSTANT)
    Q_PROPERTY(bool supportsSplitRatio READ supportsSplitRatio CONSTANT)

public:
    explicit TilingAlgorithm(QObject *parent = nullptr);
    ~TilingAlgorithm() override = default;

    // Prevent copying (QObject rule)
    TilingAlgorithm(const TilingAlgorithm &) = delete;
    TilingAlgorithm &operator=(const TilingAlgorithm &) = delete;

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
     * @brief Icon name for UI display
     * @return KDE icon name (e.g., "view-grid-symbolic")
     */
    virtual QString icon() const noexcept = 0;

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
    virtual QVector<QRect> calculateZones(const TilingParams &params) const = 0;

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
     * @brief Compute the usable area after subtracting outer gaps from screen edges
     *
     * @param screenGeometry Full screen rectangle
     * @param outerGap Gap at each edge in pixels
     * @return Inset rectangle (clamped to at least 1x1)
     */
    static QRect innerRect(const QRect &screenGeometry, int outerGap);

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
    static QVector<int> distributeWithMinSizes(int total, int count, int gap,
                                               const QVector<int> &minDims);

    /**
     * @brief Extract minimum width from minSizes at the given index
     * @return The minimum width (>= 0), or 0 if index is out of range
     */
    static int minWidthAt(const QVector<QSize> &minSizes, int index);

    /**
     * @brief Extract minimum height from minSizes at the given index
     * @return The minimum height (>= 0), or 0 if index is out of range
     */
    static int minHeightAt(const QVector<QSize> &minSizes, int index);

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