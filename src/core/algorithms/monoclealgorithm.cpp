// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "monoclealgorithm.h"

namespace PlasmaZones {

QVector<QRectF> MonocleTilingAlgorithm::generateZones(int windowCount, const TilingParams& /*params*/) const
{
    if (windowCount <= 0) {
        return {};
    }

    return {QRectF(0.0, 0.0, 1.0, 1.0)};
}

} // namespace PlasmaZones
