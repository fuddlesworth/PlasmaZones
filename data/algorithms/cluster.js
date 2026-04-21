// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

var metadata = {
    name: "Cluster",
    builtinId: "cluster",
    description: "Groups windows by application — same app windows are placed adjacent",
    producesOverlappingZones: false,
    supportsMasterCount: false,
    supportsSplitRatio: false,
    defaultMaxWindows: 8,
    minimumWindows: 1,
    zoneNumberDisplay: "all",
    supportsMemory: false,
    supportsMinSizes: false,
    customParams: [
    { name: "focusBoost", type: "number", default: 0.2, min: 0.0, max: 0.5, description: "Extra width given to the focused cluster" },
    { name: "minClusterRatio", type: "number", default: 0.1, min: 0.05, max: 0.5, description: "Minimum width ratio per cluster" }
]
};

/**
 * Cluster layout: groups windows by application identity.
 *
 * Windows with the same appId are placed in the same column (or row
 * on portrait screens), stacked within the group. Each cluster's
 * share of space is proportional to its window count, with an optional
 * boost for the cluster containing the focused window.
 *
 * Uses params.windows[].appId for grouping, params.focusedIndex for
 * boost targeting, and params.screen.portrait for axis auto-flip.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    var count = params.windowCount;
    if (count <= 0) return [];

    var area = params.area;
    var gap = params.innerGap;

    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }

    // Detect portrait orientation (needed before fallback)
    var portrait = false;
    if (params.screen) {
        portrait = !!params.screen.portrait;
    }

    // Without per-window context, clustering is meaningless — fall back to even split
    if (!params.windows || params.windows.length === 0) {
        return evenSplit(area, count, gap, portrait);
    }

    // Read custom params with defaults
    var focusBoost = 0.2;
    var minClusterRatio = 0.1;
    if (params.custom) {
        if (typeof params.custom.focusBoost === "number") {
            focusBoost = params.custom.focusBoost;
        }
        if (typeof params.custom.minClusterRatio === "number") {
            minClusterRatio = params.custom.minClusterRatio;
        }
    }

    // ── Build clusters from window appIds ───────────────────────────────
    // Preserve insertion order: first occurrence of each appId defines
    // the cluster position.
    var clusterOrder = []; // appId strings in first-seen order
    var clusterMap = {};   // appId -> { indices: [int], hasFocus: bool }

    var focusedIndex = (typeof params.focusedIndex === "number") ? params.focusedIndex : -1;
    var winLen = params.windows ? params.windows.length : 0;

    for (var i = 0; i < count; i++) {
        var appId = "unknown";
        if (i < winLen && params.windows[i].appId) {
            appId = params.windows[i].appId;
        }

        if (!clusterMap[appId]) {
            clusterMap[appId] = { indices: [], hasFocus: false };
            clusterOrder.push(appId);
        }
        clusterMap[appId].indices.push(i);
        // Only trust focusedIndex when it maps to a real window entry
        if (i === focusedIndex && i < winLen) {
            clusterMap[appId].hasFocus = true;
        }
    }

    var numClusters = clusterOrder.length;

    // ── Compute cluster sizes (proportional to window count + focus boost) ──
    // Base weight = window count in cluster. Focused cluster gets a boost
    // proportional to its own size, so single-window clusters don't get
    // disproportionately inflated.
    var totalWeight = 0;
    var weights = [];
    for (var ci = 0; ci < numClusters; ci++) {
        var cluster = clusterMap[clusterOrder[ci]];
        var w = cluster.indices.length;
        if (cluster.hasFocus && focusBoost > 0) {
            w += focusBoost * cluster.indices.length;
        }
        weights.push(w);
        totalWeight += w;
    }

    // Primary axis: width for landscape, height for portrait
    var primaryTotal = portrait ? area.height : area.width;
    var secondaryTotal = portrait ? area.width : area.height;
    var primaryStart = portrait ? area.y : area.x;
    var secondaryStart = portrait ? area.x : area.y;

    // Deduct inter-cluster gaps
    var gapSpace = (numClusters > 1) ? (numClusters - 1) * gap : 0;
    var availablePrimary = primaryTotal - gapSpace;
    if (availablePrimary <= 0) {
        return fillArea(area, count);
    }

    // Compute cluster primary dimensions with minimum enforcement.
    // Cap minClusterPx so that numClusters * minClusterPx <= availablePrimary,
    // preventing normalization from producing negative sizes.
    var rawMinPx = Math.max(PZ_MIN_ZONE_SIZE, Math.floor(availablePrimary * minClusterRatio));
    var minClusterPx = Math.min(rawMinPx, Math.floor(availablePrimary / numClusters));
    minClusterPx = Math.max(PZ_MIN_ZONE_SIZE, minClusterPx);
    var clusterSizes = [];
    var totalAssigned = 0;

    for (var ci = 0; ci < numClusters; ci++) {
        var raw = Math.floor(availablePrimary * weights[ci] / totalWeight);
        var clamped = Math.max(minClusterPx, raw);
        clusterSizes.push(clamped);
        totalAssigned += clamped;
    }

    // Normalize if total exceeds available (can happen from minCluster clamping)
    if (totalAssigned > availablePrimary) {
        var scale = availablePrimary / totalAssigned;
        totalAssigned = 0;
        for (var ci = 0; ci < numClusters; ci++) {
            clusterSizes[ci] = Math.max(PZ_MIN_ZONE_SIZE, Math.floor(clusterSizes[ci] * scale));
            totalAssigned += clusterSizes[ci];
        }
        // Distribute remainder to the last cluster (guaranteed non-negative
        // because minClusterPx is capped to availablePrimary / numClusters)
        if (totalAssigned < availablePrimary) {
            clusterSizes[numClusters - 1] += availablePrimary - totalAssigned;
        }
    } else if (totalAssigned < availablePrimary) {
        // Distribute surplus to the focused cluster, or the first cluster
        var surplus = availablePrimary - totalAssigned;
        var targetIdx = 0;
        for (var ci = 0; ci < numClusters; ci++) {
            if (clusterMap[clusterOrder[ci]].hasFocus) {
                targetIdx = ci;
                break;
            }
        }
        clusterSizes[targetIdx] += surplus;
    }

    // ── Lay out clusters and their windows ──────────────────────────────
    // Pre-fill with full-area fallback so no index is left undefined if
    // count and params.windows.length diverge during a transient race.
    var zones = [];
    for (var zi = 0; zi < count; zi++) {
        zones.push({ x: area.x, y: area.y, width: area.width, height: area.height });
    }
    var primaryPos = primaryStart;

    for (var ci = 0; ci < numClusters; ci++) {
        var cluster = clusterMap[clusterOrder[ci]];
        var clusterPrimary = clusterSizes[ci];
        var windowsInCluster = cluster.indices.length;

        // Stack windows within the cluster along the secondary axis
        var innerGapSpace = (windowsInCluster > 1) ? (windowsInCluster - 1) * gap : 0;
        var availableSecondary = secondaryTotal - innerGapSpace;
        if (availableSecondary <= 0) {
            // Degenerate: just stack all at same position
            for (var wi = 0; wi < windowsInCluster; wi++) {
                var idx = cluster.indices[wi];
                if (portrait) {
                    zones[idx] = { x: secondaryStart, y: primaryPos,
                                   width: secondaryTotal, height: clusterPrimary };
                } else {
                    zones[idx] = { x: primaryPos, y: secondaryStart,
                                   width: clusterPrimary, height: secondaryTotal };
                }
            }
        } else {
            var secondaryPerWindow = Math.floor(availableSecondary / windowsInCluster);
            var secondaryRemainder = availableSecondary - secondaryPerWindow * windowsInCluster;
            var secondaryPos = secondaryStart;

            for (var wi = 0; wi < windowsInCluster; wi++) {
                var idx = cluster.indices[wi];
                var thisSecondary = secondaryPerWindow;
                if (wi < secondaryRemainder) {
                    thisSecondary += 1;
                }

                if (portrait) {
                    zones[idx] = { x: secondaryPos, y: primaryPos,
                                   width: thisSecondary, height: clusterPrimary };
                } else {
                    zones[idx] = { x: primaryPos, y: secondaryPos,
                                   width: clusterPrimary, height: thisSecondary };
                }

                secondaryPos += thisSecondary + gap;
            }
        }

        primaryPos += clusterPrimary + gap;
    }

    return zones;
}

/**
 * Even-split fallback for preview mode (no per-window context).
 * Orientation-aware: columns in landscape, rows in portrait.
 */
function evenSplit(area, count, gap, portrait) {
    var gapSpace = (count > 1) ? (count - 1) * gap : 0;
    var total = portrait ? area.height : area.width;
    var available = total - gapSpace;
    if (available <= 0) {
        return fillArea(area, count);
    }
    var sliceSize = Math.floor(available / count);
    var remainder = available - sliceSize * count;
    var zones = [];
    var pos = portrait ? area.y : area.x;
    for (var i = 0; i < count; i++) {
        var s = sliceSize + (i < remainder ? 1 : 0);
        if (portrait) {
            zones.push({ x: area.x, y: pos, width: area.width, height: s });
        } else {
            zones.push({ x: pos, y: area.y, width: s, height: area.height });
        }
        pos += s + gap;
    }
    return zones;
}
