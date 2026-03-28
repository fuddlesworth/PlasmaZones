// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Binary Split
// @builtinId bsp
// @description Balanced recursive splitting into equal regions
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.5
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory false

/**
 * BSP (Binary Space Partitioning) tiling algorithm.
 *
 * Builds a balanced binary tree from scratch by repeatedly splitting the
 * largest leaf. Each internal node stores a split direction and ratio.
 * Deterministic: same inputs always produce the same layout.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */

var MaxBSPDepth = 50;

function calculateZones(params) {
    var count = params.windowCount;
    if (count <= 0) return [];

    var area = params.area;
    var gap = params.innerGap || 0;
    var minSizes = params.minSizes || [];
    var splitRatio = params.splitRatio;

    // Build a fresh tree from scratch each time for deterministic output
    var root = buildTree(count, splitRatio, area);

    // Apply geometry top-down with inner gaps at each split point
    bspApplyGeometry(root, area, gap, minSizes, 0, 0);

    // Collect leaf geometries
    var zones = [];
    collectLeaves(root, zones, 0);

    // Validate that all zones have positive dimensions
    var hasInvalidZone = false;
    for (var i = 0; i < zones.length; i++) {
        if (zones[i].width <= 0 || zones[i].height <= 0) {
            hasInvalidZone = true;
            break;
        }
    }

    if (hasInvalidZone) {
        // Fall back to gap-aware equal columns layout
        zones = [];
        var minWidthVec = [];
        if (minSizes.length > 0) {
            for (var i = 0; i < count && i < minSizes.length; i++) {
                minWidthVec.push(minSizes[i].w || 0);
            }
        }
        var columnWidths = minSizes.length === 0
            ? distributeWithGaps(area.width, count, gap)
            : distributeWithMinSizes(area.width, count, gap, minWidthVec);
        var currentX = area.x;
        for (var i = 0; i < count; i++) {
            zones.push({x: currentX, y: area.y, width: columnWidths[i], height: area.height});
            currentX += columnWidths[i] + gap;
        }
    }

    return zones;
}

// ─── Tree construction ──────────────────────────────────────────────────────

function makeNode() {
    return {
        first: null,
        second: null,
        geometry: {x: 0, y: 0, width: 0, height: 0},
        cachedArea: 0,
        splitHorizontal: false,
        splitRatio: 0.5
    };
}

function isLeaf(node) {
    return !node.first && !node.second;
}

function buildTree(windowCount, defaultRatio, refRect) {
    if (windowCount <= 0) return null;

    // Start with a single leaf as root
    var root = makeNode();

    // Use actual screen geometry so split direction heuristics match the
    // real screen. Falls back to 1920x1080 if the provided rect is invalid.
    var buildRect = (refRect && refRect.width > 0 && refRect.height > 0)
        ? refRect
        : {x: 0, y: 0, width: 1920, height: 1080};

    var leafCount = 1;
    var maxIterations = 1000;
    var iterations = 0;
    while (leafCount < windowCount && iterations < maxIterations) {
        iterations++;
        // Apply geometry so largestLeaf can find the optimal split candidate
        bspApplyGeometry(root, buildRect, 0, [], 0, 0);
        var result = growTree(root, defaultRatio);
        if (!result) break;
        leafCount++;
    }

    return root;
}

function growTree(root, defaultRatio) {
    if (!root) return false;

    // Find the largest leaf to split (produces balanced layouts)
    var leaf = findLargestLeaf(root, 0);
    if (!leaf || !isLeaf(leaf)) return false;

    // Split this leaf into an internal node with two leaf children
    leaf.first = makeNode();
    leaf.second = makeNode();

    // Choose split direction based on current geometry (if available) or default
    if (leaf.geometry.width > 0 && leaf.geometry.height > 0) {
        leaf.splitHorizontal = chooseSplitDirection(leaf.geometry);
    } else {
        // Estimate: count depth from root and alternate
        // (no parent pointer in JS version, but geometry should always be set
        //  since we call bspApplyGeometry before growTree)
        leaf.splitHorizontal = false;
    }

    leaf.splitRatio = defaultRatio;
    return true;
}

// ─── Geometry computation (top-down) ────────────────────────────────────────

function computeSubtreeMinDims(node, minSizes, leafStartIdx, innerGap) {
    if (!node) return {w: 0, h: 0, leafCount: 0};

    if (isLeaf(node)) {
        if (leafStartIdx < minSizes.length) {
            var ms = minSizes[leafStartIdx];
            return {w: Math.max(ms.w || 0, 0), h: Math.max(ms.h || 0, 0), leafCount: 1};
        }
        return {w: 0, h: 0, leafCount: 1};
    }

    var firstResult = computeSubtreeMinDims(node.first, minSizes, leafStartIdx, innerGap);
    var secondResult = computeSubtreeMinDims(node.second, minSizes, leafStartIdx + firstResult.leafCount, innerGap);
    var totalLeaves = firstResult.leafCount + secondResult.leafCount;

    if (node.splitHorizontal) {
        // Top/bottom split: width = max, height = sum + gap
        return {
            w: Math.max(firstResult.w, secondResult.w),
            h: firstResult.h + innerGap + secondResult.h,
            leafCount: totalLeaves
        };
    } else {
        // Left/right split: width = sum + gap, height = max
        return {
            w: firstResult.w + innerGap + secondResult.w,
            h: Math.max(firstResult.h, secondResult.h),
            leafCount: totalLeaves
        };
    }
}

function clampOrProportionalFallback(ratio, minFirstRatio, maxFirstRatio, firstDim, secondDim) {
    if (minFirstRatio <= maxFirstRatio) {
        return Math.max(minFirstRatio, Math.min(maxFirstRatio, ratio));
    }
    var totalMin = firstDim + secondDim;
    if (totalMin > 0) {
        ratio = firstDim / totalMin;
        return Math.max(PZ_MIN_SPLIT, Math.min(PZ_MAX_SPLIT, ratio));
    }
    return ratio;
}

function bspApplyGeometry(node, rect, innerGap, minSizes, leafStartIdx, depth) {
    if (!node || depth > MaxBSPDepth) return;

    node.geometry = {x: rect.x, y: rect.y, width: rect.width, height: rect.height};
    node.cachedArea = (rect.width > 0 && rect.height > 0) ? rect.width * rect.height : 0;

    if (isLeaf(node)) return;

    // Clamp ratio
    var ratio = Math.max(PZ_MIN_SPLIT, Math.min(PZ_MAX_SPLIT, node.splitRatio));

    // Clamp ratio to respect subtree minimum dimensions
    if (minSizes.length > 0) {
        var firstResult = computeSubtreeMinDims(node.first, minSizes, leafStartIdx, innerGap);
        var secondResult = computeSubtreeMinDims(node.second, minSizes,
            leafStartIdx + firstResult.leafCount, innerGap);

        if (node.splitHorizontal) {
            var contentHeight = rect.height - innerGap;
            if (contentHeight > 0 && (firstResult.h > 0 || secondResult.h > 0)) {
                var minFirstRatio = (firstResult.h > 0) ? firstResult.h / contentHeight : PZ_MIN_SPLIT;
                var maxFirstRatio = (secondResult.h > 0) ? 1.0 - secondResult.h / contentHeight : PZ_MAX_SPLIT;
                minFirstRatio = Math.max(PZ_MIN_SPLIT, Math.min(PZ_MAX_SPLIT, minFirstRatio));
                maxFirstRatio = Math.max(PZ_MIN_SPLIT, Math.min(PZ_MAX_SPLIT, maxFirstRatio));
                ratio = clampOrProportionalFallback(ratio, minFirstRatio, maxFirstRatio,
                    firstResult.h, secondResult.h);
            }
        } else {
            var contentWidth = rect.width - innerGap;
            if (contentWidth > 0 && (firstResult.w > 0 || secondResult.w > 0)) {
                var minFirstRatio = (firstResult.w > 0) ? firstResult.w / contentWidth : PZ_MIN_SPLIT;
                var maxFirstRatio = (secondResult.w > 0) ? 1.0 - secondResult.w / contentWidth : PZ_MAX_SPLIT;
                minFirstRatio = Math.max(PZ_MIN_SPLIT, Math.min(PZ_MAX_SPLIT, minFirstRatio));
                maxFirstRatio = Math.max(PZ_MIN_SPLIT, Math.min(PZ_MAX_SPLIT, maxFirstRatio));
                ratio = clampOrProportionalFallback(ratio, minFirstRatio, maxFirstRatio,
                    firstResult.w, secondResult.w);
            }
        }
    }

    // Count first child leaves for leaf index threading
    var firstChildLeaves = countLeavesInSubtree(node.first, 0);

    if (node.splitHorizontal) {
        // Split top/bottom with innerGap between children
        var contentHeight = rect.height - innerGap;
        if (contentHeight <= 0) return;
        var firstHeight = Math.floor(contentHeight * ratio);
        var secondHeight = contentHeight - firstHeight;
        var firstRect = {x: rect.x, y: rect.y, width: rect.width, height: firstHeight};
        var secondRect = {x: rect.x, y: rect.y + firstHeight + innerGap, width: rect.width, height: secondHeight};

        // Guard: skip split if either partition is degenerate
        if (firstRect.width <= 0 || firstRect.height <= 0 || secondRect.width <= 0 || secondRect.height <= 0) return;

        bspApplyGeometry(node.first, firstRect, innerGap, minSizes, leafStartIdx, depth + 1);
        bspApplyGeometry(node.second, secondRect, innerGap, minSizes, leafStartIdx + firstChildLeaves, depth + 1);
    } else {
        // Split left/right with innerGap between children
        var contentWidth = rect.width - innerGap;
        if (contentWidth <= 0) return;
        var firstWidth = Math.floor(contentWidth * ratio);
        var secondWidth = contentWidth - firstWidth;
        var firstRect = {x: rect.x, y: rect.y, width: firstWidth, height: rect.height};
        var secondRect = {x: rect.x + firstWidth + innerGap, y: rect.y, width: secondWidth, height: rect.height};

        // Guard: skip split if either partition is degenerate
        if (firstRect.width <= 0 || firstRect.height <= 0 || secondRect.width <= 0 || secondRect.height <= 0) return;

        bspApplyGeometry(node.first, firstRect, innerGap, minSizes, leafStartIdx, depth + 1);
        bspApplyGeometry(node.second, secondRect, innerGap, minSizes, leafStartIdx + firstChildLeaves, depth + 1);
    }
}

function collectLeaves(node, zones, depth) {
    if (!node || depth > MaxBSPDepth) return;

    if (isLeaf(node)) {
        zones.push({x: node.geometry.x, y: node.geometry.y,
                     width: node.geometry.width, height: node.geometry.height});
    } else {
        collectLeaves(node.first, zones, depth + 1);
        collectLeaves(node.second, zones, depth + 1);
    }
}

// ─── Tree traversal helpers ─────────────────────────────────────────────────

function countLeavesInSubtree(node, depth) {
    if (!node || depth > MaxBSPDepth) return 0;
    if (isLeaf(node)) return 1;
    return countLeavesInSubtree(node.first, depth + 1) + countLeavesInSubtree(node.second, depth + 1);
}

function findLargestLeaf(node, depth) {
    if (!node || depth > MaxBSPDepth) return null;
    if (isLeaf(node)) return node;

    var left = findLargestLeaf(node.first, depth + 1);
    var right = findLargestLeaf(node.second, depth + 1);

    if (!left) return right;
    if (!right) return left;

    var leftArea = left.cachedArea;
    var rightArea = right.cachedArea;

    // Fallback to right (deepest) when no geometry is available
    if (leftArea === 0 && rightArea === 0) return right;

    return (leftArea >= rightArea) ? left : right;
}

function chooseSplitDirection(geometry) {
    // Split perpendicular to longest axis for balanced regions
    return geometry.height > geometry.width;
}
