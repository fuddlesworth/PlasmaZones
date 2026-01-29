// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"
#include <memory>

namespace PlasmaZones {

/**
 * @brief Binary Space Partitioning tiling algorithm
 *
 * BSP recursively divides the screen into smaller rectangles using a
 * binary tree structure. Each window occupies a leaf node. The split
 * direction alternates based on the aspect ratio of each region
 * (split perpendicular to the longest side).
 *
 * Layout example (5 windows):
 * ```
 * +-------------+-------------+
 * |             |             |
 * |      1      |      2      |
 * |             |             |
 * +-------------+------+------+
 * |             |      |      |
 * |      3      |  4   |  5   |
 * |             |      |      |
 * +-------------+------+------+
 * ```
 *
 * Features:
 * - Automatic split direction based on aspect ratio
 * - Configurable split ratio
 * - Balanced distribution of space
 * - Works well with any number of windows
 */
class PLASMAZONES_EXPORT BSPAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit BSPAlgorithm(QObject *parent = nullptr);
    ~BSPAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;
    QString icon() const override;

    QVector<QRect> calculateZones(int windowCount, const QRect &screenGeometry,
                                  const TilingState &state) const override;

    bool supportsMasterCount() const override { return false; }
    bool supportsSplitRatio() const override { return true; }
    qreal defaultSplitRatio() const override { return 0.5; }

private:
    /**
     * @brief BSP tree node
     */
    struct BSPNode {
        QRect geometry;
        std::unique_ptr<BSPNode> first;
        std::unique_ptr<BSPNode> second;
        bool isLeaf() const { return !first && !second; }
    };

    /**
     * @brief Recursively partition space for N windows
     *
     * @note The user's splitRatio is only applied to the first (root) split.
     *       All subsequent splits use a fixed 0.5 ratio to ensure balanced
     *       distribution of space. This matches typical BSP tiling behavior.
     *
     * @param node Current node to potentially split
     * @param windowsRemaining Number of windows still needing space
     * @param splitRatio Ratio for splitting (0.0-1.0)
     */
    void partition(BSPNode *node, int windowsRemaining, qreal splitRatio) const;

    /**
     * @brief Collect leaf node geometries in order
     * @param node Root of subtree
     * @param zones Output vector
     */
    void collectLeaves(const BSPNode *node, QVector<QRect> &zones) const;
};

} // namespace PlasmaZones
