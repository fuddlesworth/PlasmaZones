// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace PlasmaZones {
namespace ScriptedHelpers {

/**
 * @brief JS source for distributeWithGaps(total, count, gap) built-in helper
 *
 * Port of TilingAlgorithm::distributeWithGaps. Deducts (count-1)*gap from
 * total, distributes remainder evenly. Returns array of int sizes.
 * Loaded from Qt resource :/builtins/distributeWithGaps.js
 */
QString distributeWithGapsJs();

/**
 * @brief JS source for distributeWithMinSizes(total, count, gap, minDims) built-in helper
 *
 * Port of TilingAlgorithm::distributeWithMinSizes. Complex distribution with
 * proportional fallback when unsatisfiable, unconstrained-surplus optimization.
 * Returns array of int sizes.
 * Loaded from Qt resource :/builtins/distributeWithMinSizes.js
 */
QString distributeWithMinSizesJs();

/**
 * @brief JS source for solveTwoPart(contentDim, firstDim, secondDim, minFirst, minSecond) built-in helper
 *
 * Port of TilingAlgorithm::solveTwoPartMinSizes. Returns {first, second}.
 * Loaded from Qt resource :/builtins/solveTwoPart.js
 */
QString solveTwoPartJs();

/**
 * @brief JS source for solveThreeColumn(...) built-in helper
 *
 * Port of TilingAlgorithm::solveThreeColumnWidths. Uses injected constants
 * PZ_MIN_ZONE_SIZE, PZ_MIN_SPLIT, PZ_MAX_SPLIT. Returns object with
 * leftWidth, centerWidth, rightWidth, leftX, centerX, rightX.
 * Loaded from Qt resource :/builtins/solveThreeColumn.js
 */
QString solveThreeColumnJs();

/**
 * @brief JS source for computeCumulativeMinDims(windowCount, minSizes, innerGap) built-in helper
 *
 * Port of TilingAlgorithm::computeAlternatingCumulativeMinDims.
 * Returns {minW: [], minH: []}.
 * Loaded from Qt resource :/builtins/computeCumulativeMinDims.js
 */
QString cumulativeMinDimsJs();

/**
 * @brief JS source for appendGracefulDegradation(zones, remaining, leftover, innerGap) built-in helper
 *
 * Port of TilingAlgorithm::appendGracefulDegradation. Modifies zones array in place.
 * Loaded from Qt resource :/builtins/appendGracefulDegradation.js
 */
QString gracefulDegradationJs();

/**
 * @brief JS source for dwindleLayout(area, count, splitRatio, innerGap, minSizes) built-in helper
 *
 * Port of DwindleAlgorithm::calculateZones core logic (without outer gap/
 * single-window handling). Returns array of zone objects.
 * Loaded from Qt resource :/builtins/dwindleLayout.js
 */
QString dwindleLayoutJs();

/**
 * @brief JS source for extractMinWidths(minSizes, count) and extractMinHeights(minSizes, count) helpers
 *
 * Convenience functions that extract per-element minimum width/height
 * values from a minSizes array. Returns empty array when minSizes is empty,
 * allowing callers to branch between distributeWithGaps and distributeWithMinSizes.
 * Loaded from Qt resource :/builtins/extractMinDims.js
 */
QString extractMinDimsJs();

/**
 * @brief JS source for stack interleaving helpers shared by centered-master and three-column
 *
 * Provides buildStackIsLeft(), interleaveMinWidths(), interleaveMinHeights(),
 * and assignInterleavedStacks() for DRY left/right column interleaving.
 * Loaded from Qt resource :/builtins/interleaveStacks.js
 */
QString interleaveStacksJs();

} // namespace ScriptedHelpers
} // namespace PlasmaZones
