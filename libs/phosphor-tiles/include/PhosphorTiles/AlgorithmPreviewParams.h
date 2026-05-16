// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>

#include <QHash>
#include <QString>
#include <QVariantMap>

namespace PhosphorTiles {

/// User-configured tiling parameters that affect algorithm preview
/// generation. Lives at namespace scope (rather than nested inside
/// @c AlgorithmRegistry) so the abstract @c ITileAlgorithmRegistry
/// contract can carry it without the interface depending on the
/// concrete registry type — which would create a circular include.
struct PHOSPHORTILES_EXPORT AlgorithmPreviewParams
{
    QString algorithmId; ///< Active algorithm — maxWindows/splitRatio/masterCount apply only to this
    int maxWindows = -1; ///< -1 = use algorithm default
    int masterCount = -1; ///< -1 = use default (1)
    qreal splitRatio = -1.0; ///< -1 = use algorithm default

    /// Per-algorithm saved settings (masterCount, splitRatio).
    /// Generalised replacement for hard-coded centered-master fields.
    /// Key = algorithm ID, value = QVariantMap with @c "masterCount"
    /// (int) and @c "splitRatio" (qreal).
    QHash<QString, QVariantMap> savedAlgorithmSettings;

    bool operator==(const AlgorithmPreviewParams& other) const;
    bool operator!=(const AlgorithmPreviewParams& other) const
    {
        return !(*this == other);
    }
};

} // namespace PhosphorTiles
