// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace PhosphorTiles {
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

/**
 * @brief JS source for applyPerWindowMinSize(w, h, minSizes, i) built-in helper
 *
 * Applies per-window minimum size constraints from the minSizes array.
 * Returns {w, h} with dimensions clamped to minimums.
 * Loaded from Qt resource :/builtins/applyPerWindowMinSize.js
 */
QString applyPerWindowMinSizeJs();

/**
 * @brief JS source for extractRegionMaxMin(minSizes, startIdx, endIdx, axis) built-in helper
 *
 * Scans a region of the minSizes array for the maximum minimum dimension
 * on the given axis ('w' or 'h'). Used by master-stack layouts.
 * Loaded from Qt resource :/builtins/extractRegionMaxMin.js
 */
QString extractRegionMaxMinJs();

/**
 * @brief JS source for fillArea(area, count) built-in helper
 *
 * Creates an array of identical zones covering the full area.
 * Used as a degenerate-screen fallback.
 * Loaded from Qt resource :/builtins/fillArea.js
 */
QString fillAreaJs();

/**
 * @brief JS source for masterStackLayout(area, count, gap, splitRatio, masterCount, minSizes, horizontal) helper
 *
 * Shared implementation for master-stack and wide layouts.
 * horizontal=false: master left, stack right. horizontal=true: master top, stack bottom.
 * Loaded from Qt resource :/builtins/masterStackLayout.js
 */
QString masterStackLayoutJs();

/**
 * @brief JS source for applyTreeGeometry(node, rect, gap, _depth) built-in helper
 *
 * Recursively computes zone rectangles from a binary split tree.
 * Depth-limited to MAX_TREE_DEPTH (must match AutotileDefaults::MaxRuntimeTreeDepth).
 * Loaded from Qt resource :/builtins/applyTreeGeometry.js
 */
QString applyTreeGeometryJs();

/**
 * @brief JS source for lShapeLayout(area, count, gap, splitRatio, distribute, bottomWidth, rightHeight) helper
 *
 * L-shaped tiling with master in top-left and secondary zones along right/bottom edges.
 * Uses injected PZ_MIN_SPLIT/PZ_MAX_SPLIT globals.
 * Loaded from Qt resource :/builtins/lShapeLayout.js
 */
QString lShapeLayoutJs();

/**
 * @brief JS source for deckLayout(area, count, focusedFraction, horizontal) built-in helper
 *
 * Deck (monocle-with-peek) layout where the focused window takes a large
 * fraction and background windows peek from the remainder.
 * Loaded from Qt resource :/builtins/deckLayout.js
 */
QString deckLayoutJs();

/**
 * @brief JS source for distributeEvenly(start, total, count, gap) built-in helper
 *
 * Distributes items evenly along a 1D axis with gaps. Returns array of
 * {pos, size} objects with the last item absorbing rounding remainder.
 * Loaded from Qt resource :/builtins/distributeEvenly.js
 */
QString distributeEvenlyJs();

/**
 * @brief JS source for equalColumnsLayout(area, count, gap, minSizes) built-in helper
 *
 * Equal-width columns layout with gap-aware distribution. Used as a
 * standalone layout (columns algorithm) and as a fallback when other
 * algorithms produce degenerate zones.
 * Loaded from Qt resource :/builtins/equalColumnsLayout.js
 */
QString equalColumnsLayoutJs();

/**
 * @brief JS source for fillRegion(x, y, w, h, count) built-in helper
 *
 * Creates an array of identical zones covering the same region.
 * Used as a degenerate-gap fallback when gaps exceed available space.
 * Unlike fillArea(), this takes explicit x/y/w/h for partial regions.
 * Loaded from Qt resource :/builtins/fillRegion.js
 */
QString fillRegionJs();

/**
 * @brief JS source for distributeWithOptionalMins(total, count, gap, minDims) built-in helper
 *
 * Wraps the repeated ternary pattern that branches on minDims.length,
 * delegating to distributeWithGaps when empty or distributeWithMinSizes otherwise.
 * Loaded from Qt resource :/builtins/distributeWithOptionalMins.js
 */
QString distributeWithOptionalMinsJs();

/**
 * @brief JS source for threeColumnLayout(area, count, gap, splitRatio, masterCount, minSizes) helper
 *
 * Shared implementation for centered-master and three-column layouts.
 * masterCount=1: three-column behavior. masterCount>1: centered-master behavior.
 * Loaded from Qt resource :/builtins/threeColumnLayout.js
 */
QString threeColumnLayoutJs();

/**
 * @brief JS source for clampSplitRatio(ratio) built-in helper
 *
 * Clamps a ratio to [PZ_MIN_SPLIT, PZ_MAX_SPLIT]. Eliminates the repeated
 * Math.max(PZ_MIN_SPLIT, Math.min(PZ_MAX_SPLIT, ratio)) pattern.
 * Loaded from Qt resource :/builtins/clampSplitRatio.js
 */
QString clampSplitRatioJs();

} // namespace ScriptedHelpers
} // namespace PhosphorTiles
