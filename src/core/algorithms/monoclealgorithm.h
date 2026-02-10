// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/tilingalgorithm.h"

namespace PlasmaZones {

/**
 * @brief Single fullscreen zone — all windows stack in one zone
 *
 * Equivalent to Krohnkite's Monocle layout.
 * Ignores masterRatio and masterCount entirely.
 */
class PLASMAZONES_EXPORT MonocleTilingAlgorithm : public TilingAlgorithm
{
public:
    QString id() const override { return QStringLiteral("monocle"); }
    QString name() const override { return QStringLiteral("Monocle"); }
    QString description() const override { return QStringLiteral("Single fullscreen zone (all windows stacked)"); }
    QVector<QRectF> generateZones(int windowCount, const TilingParams& params) const override;
};

} // namespace PlasmaZones
