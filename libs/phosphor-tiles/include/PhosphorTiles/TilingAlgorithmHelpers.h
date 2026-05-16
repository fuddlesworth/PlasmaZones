// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>

#include <QVector>

namespace PhosphorTiles {

/**
 * @brief Result of solving three-column width distribution
 *
 * Shared by ThreeColumnAlgorithm and CenteredMasterAlgorithm via
 * @c TilingAlgorithm::solveThreeColumnWidths. Exposed as a namespace-level
 * type so consumers / tests can reference it without the enclosing class
 * context; the class still provides @c TilingAlgorithm::ThreeColumnWidths
 * as a type alias for source compatibility.
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
 * @brief Result of precomputing cumulative min dimensions for alternating V/H splits
 *
 * Shared by Dwindle and Spiral algorithms via
 * @c TilingAlgorithm::computeAlternatingCumulativeMinDims. Exposed as a
 * namespace-level type so consumers / tests can reference it without the
 * enclosing class context; the class still provides
 * @c TilingAlgorithm::CumulativeMinDims as a type alias for source
 * compatibility.
 */
struct CumulativeMinDims
{
    QVector<int> minW; ///< Per-window cumulative minimum width (size = windowCount + 1)
    QVector<int> minH; ///< Per-window cumulative minimum height (size = windowCount + 1)
};

} // namespace PhosphorTiles
