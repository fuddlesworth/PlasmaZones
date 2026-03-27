// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {
class DwindleAlgorithm;

/**
 * @brief Dwindle tiling algorithm with persistent split memory
 *
 * Like DwindleAlgorithm, recursively subdivides space using alternating
 * vertical/horizontal splits. Unlike DwindleAlgorithm, this variant uses
 * a persistent SplitTree so that resizing one split does not affect others.
 *
 * When the SplitTree matches the current window count, zone geometries are
 * computed from the tree. Otherwise, falls back to stateless dwindle logic
 * identical to DwindleAlgorithm.
 *
 * Layout example (5 windows, ratio=0.5):
 * ```
 * +----------+---------+
 * |          |    2    |
 * |    1     +----+----+
 * |          | 3  | 4  |
 * |          |    +----+
 * |          |    | 5  |
 * +----------+----+----+
 * ```
 */
class PLASMAZONES_EXPORT DwindleMemoryAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit DwindleMemoryAlgorithm(QObject* parent = nullptr);
    ~DwindleMemoryAlgorithm() override;

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;

    QVector<QRect> calculateZones(const TilingParams& params) const override;

    bool supportsMasterCount() const override
    {
        return false;
    }
    bool supportsSplitRatio() const override
    {
        return true;
    }
    int defaultMaxWindows() const override
    {
        return 5;
    }
    bool supportsMemory() const noexcept override
    {
        return true;
    }
    [[nodiscard]] qreal defaultSplitRatio() const override
    {
        return 0.5;
    }

    /**
     * @brief Prepare the TilingState, ensuring it has a SplitTree
     *
     * Override of TilingAlgorithm::prepareTilingState(). Called by the engine
     * before calculateZones(). Only creates a tree when windowCount > 1 and
     * no tree exists.
     */
    void prepareTilingState(TilingState* state) const override;

private:
    DwindleAlgorithm* m_fallback = nullptr; ///< Stateless fallback (owned via QObject parent)
    QVector<QRect> calculateStatelessFallback(const TilingParams& params) const;
};

} // namespace PlasmaZones
