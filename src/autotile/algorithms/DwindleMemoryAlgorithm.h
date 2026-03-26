// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"
#include "DwindleAlgorithm.h"

namespace PlasmaZones {

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
    ~DwindleMemoryAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;

    QVector<QRect> calculateZones(const TilingParams& params) const override;

    bool supportsSplitRatio() const noexcept override
    {
        return true;
    }
    int defaultMaxWindows() const noexcept override
    {
        return 5;
    }
    bool supportsMemory() const noexcept override
    {
        return true;
    }

    /**
     * @brief Ensure the TilingState has a SplitTree, creating one lazily if needed
     *
     * Called by the engine before calculateZones() so the algorithm itself
     * does not need to const_cast the state. Only creates a tree when
     * windowCount > 1 and no tree exists.
     */
    void ensureSplitTree(TilingState* state) const;

private:
    DwindleAlgorithm m_fallback; ///< Stateless fallback (avoids static QObject)
    QVector<QRect> calculateStatelessFallback(const TilingParams& params, const QRect& area) const;
};

} // namespace PlasmaZones
