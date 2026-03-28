// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Recursively compute zone rectangles from a binary split tree.
 *
 * Each tree node has: { horizontal, ratio, first, second } for splits,
 * or { windowId } for leaf nodes. The function walks the tree depth-first,
 * splitting the rectangle according to each node's orientation and ratio.
 *
 * @param {Object|null} node - Tree node (split or leaf)
 * @param {Object} rect - {x, y, width, height} bounding rectangle
 * @param {number} gap - Inner gap between sibling zones
 * @param {number} [_depth=0] - Current recursion depth (internal)
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */

// Must match AutotileDefaults::MaxRuntimeTreeDepth in core/constants.h
// Use var so the property attaches to the global object and can be frozen
// by Object.defineProperty in the C++ sandbox (const is block-scoped in V4).
var MAX_TREE_DEPTH = 50;

function applyTreeGeometry(node, rect, gap, _depth) {
    _depth = _depth || 0;
    if (_depth > MAX_TREE_DEPTH) return [];
    if (!node) return [];
    if (node.windowId !== undefined && node.windowId !== '') {
        return [{x: rect.x, y: rect.y, width: rect.width, height: rect.height}];
    }
    if (!node.first || !node.second) {
        return [{x: rect.x, y: rect.y, width: rect.width, height: rect.height}];
    }
    const ratio = Math.max(0.1, Math.min(0.9, node.ratio || 0.5));
    let zones = [];
    let content;
    if (node.horizontal) {
        content = rect.height - gap;
        if (content <= 0) {
            zones = zones.concat(applyTreeGeometry(node.first, rect, 0, _depth + 1));
            zones = zones.concat(applyTreeGeometry(node.second, rect, 0, _depth + 1));
        } else {
            const h1 = Math.floor(content * ratio);
            const h2 = content - h1;
            zones = zones.concat(applyTreeGeometry(node.first,
                {x: rect.x, y: rect.y, width: rect.width, height: h1}, gap, _depth + 1));
            zones = zones.concat(applyTreeGeometry(node.second,
                {x: rect.x, y: rect.y + h1 + gap, width: rect.width, height: h2}, gap, _depth + 1));
        }
    } else {
        content = rect.width - gap;
        if (content <= 0) {
            zones = zones.concat(applyTreeGeometry(node.first, rect, 0, _depth + 1));
            zones = zones.concat(applyTreeGeometry(node.second, rect, 0, _depth + 1));
        } else {
            const w1 = Math.floor(content * ratio);
            const w2 = content - w1;
            zones = zones.concat(applyTreeGeometry(node.first,
                {x: rect.x, y: rect.y, width: w1, height: rect.height}, gap, _depth + 1));
            zones = zones.concat(applyTreeGeometry(node.second,
                {x: rect.x + w1 + gap, y: rect.y, width: w2, height: rect.height}, gap, _depth + 1));
        }
    }
    return zones;
}
