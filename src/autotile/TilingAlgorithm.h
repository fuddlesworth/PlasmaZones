// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QRect>
#include <QString>
#include <QVector>

namespace PlasmaZones {

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
 * - name(), description(), icon() for UI display
 * - calculateZones() for the actual algorithm
 *
 * Optionally override capability methods to indicate support for:
 * - Master count adjustment
 * - Split ratio adjustment
 *
 * @note Thread Safety: Algorithm instances are stateless and all public methods
 *       are const (except QObject infrastructure). Multiple threads can safely
 *       call calculateZones() on the same instance concurrently. However, the
 *       TilingState parameter must not be modified during the call.
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
    explicit TilingAlgorithm(QObject* parent = nullptr);
    ~TilingAlgorithm() override = default;

    // Prevent copying (QObject rule)
    TilingAlgorithm(const TilingAlgorithm&) = delete;
    TilingAlgorithm& operator=(const TilingAlgorithm&) = delete;

    /**
     * @brief Human-readable name of the algorithm
     * @return Algorithm name (e.g., "Master + Stack", "BSP")
     */
    virtual QString name() const noexcept = 0;

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
     * This is the core algorithm method. Given a window count, screen geometry,
     * and current tiling state, generate zone rectangles for each window.
     *
     * @param windowCount Number of windows to tile (>= 0)
     * @param screenGeometry Available screen area in absolute pixels
     * @param state Current tiling state (master count, split ratio, etc.)
     * @return Vector of zone geometries in absolute pixel coordinates
     *
     * @note The returned vector should have exactly windowCount elements.
     *       For windowCount == 0, return an empty vector.
     *       For windowCount == 1, typically return a single full-screen zone.
     */
    virtual QVector<QRect> calculateZones(int windowCount, const QRect& screenGeometry,
                                          const TilingState& state) const = 0;

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
     * @brief Apply gaps to zone geometries
     *
     * Utility method to apply consistent gap spacing to calculated zones.
     * Called after calculateZones() when gaps are enabled.
     *
     * @param zones Zones to modify (in-place), in absolute pixel coordinates
     * @param screenGeometry Screen bounds for edge detection
     * @param innerGap Gap between adjacent zones in pixels
     * @param outerGap Gap at screen edges in pixels
     */
    static void applyGaps(QVector<QRect>& zones, const QRect& screenGeometry, int innerGap, int outerGap);

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
