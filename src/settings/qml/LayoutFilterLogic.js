// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library

/**
 * Shared filtering, grouping, and sorting helpers for the layout/algorithm grid.
 *
 * Functions receive data and filter config without accessing any QML context.
 * i18n-dependent labels are passed in by the caller.
 *
 * NOTE: sortItems() sorts group arrays in-place for efficiency.
 */

// ── Predicates ──────────────────────────────────────────────────────────────

// Both fields are checked for all item types — snapping layouts typically
// set hasSystemOrigin while algorithms set isSystem; the other is undefined/false.
function isBuiltIn(item) {
    return item.isSystem || item.hasSystemOrigin;
}

function matchesCommonFilters(item, search, showHidden) {
    if (search.length > 0
        && !(item.name || "").toLowerCase().includes(search)
        && !(item.description || "").toLowerCase().includes(search))
        return false;
    if (!showHidden && item.hiddenFromSelector === true)
        return false;
    return true;
}

// ── Filtering ───────────────────────────────────────────────────────────────

function applySnappingFilters(items, search, f) {
    var arMap = {
        "any": f.showAspectAny,
        "standard": f.showAspectStandard,
        "ultrawide": f.showAspectUltrawide,
        "super-ultrawide": f.showAspectSuperUltrawide,
        "portrait": f.showAspectPortrait
    };
    return items.filter(function(item) {
        if (!matchesCommonFilters(item, search, f.showHidden))
            return false;
        // Items with no aspectRatioClass always pass — don't conflate with "any"
        var cls = item.aspectRatioClass || "";
        if (cls !== "" && arMap[cls] === false)
            return false;
        if (isBuiltIn(item) && !f.showBuiltInLayouts)
            return false;
        if (!isBuiltIn(item) && !f.showUserLayouts)
            return false;
        if (item.autoAssign === true && !f.showAutoLayouts)
            return false;
        if (item.autoAssign !== true && !f.showManualLayouts)
            return false;
        return true;
    });
}

// Capability filters (showMasterCount, etc.) follow the same show/hide pattern
// as source filters: all ON = show everything; unchecking one hides items
// with that capability (e.g., unchecking "Master Count" hides algorithms
// that support adjustable master count).
function applyTilingFilters(items, search, f) {
    return items.filter(function(item) {
        if (!matchesCommonFilters(item, search, f.showHidden))
            return false;
        if (isBuiltIn(item) && !f.showBuiltInAlgorithms)
            return false;
        if (!isBuiltIn(item) && !f.showUserAlgorithms)
            return false;
        if (!f.showMasterCount && item.supportsMasterCount === true)
            return false;
        if (!f.showSplitRatio && item.supportsSplitRatio === true)
            return false;
        if (!f.showOverlapping && item.producesOverlappingZones === true)
            return false;
        if (!f.showPersistent && item.memory === true)
            return false;
        return true;
    });
}

// ── Grouping ────────────────────────────────────────────────────────────────

function groupByBoolKey(items, testFn, trueKey, trueLabel, falseKey, falseLabel) {
    var groups = {};
    for (var i = 0; i < items.length; i++) {
        var item = items[i];
        var match = testFn(item);
        var key = match ? trueKey : falseKey;
        if (!groups[key])
            groups[key] = {
                items: [],
                order: match ? 0 : 1,
                label: match ? trueLabel : falseLabel
            };
        groups[key].items.push(item);
    }
    return groups;
}

function groupByAspectRatio(items) {
    var groups = {};
    for (var i = 0; i < items.length; i++) {
        var key = items[i].sectionKey || "default";
        if (!groups[key])
            groups[key] = {
                items: [],
                order: items[i].sectionOrder !== undefined ? items[i].sectionOrder : 0,
                label: items[i].sectionLabel || ""
            };
        groups[key].items.push(items[i]);
    }
    return groups;
}

function groupByZoneCount(items, pluralFn, unknownLabel) {
    var groups = {};
    for (var i = 0; i < items.length; i++) {
        var count = Math.max(0, items[i].zoneCount || 0);
        var key = count > 0 ? "zones-" + count : "zones-unknown";
        if (!groups[key])
            groups[key] = {
                items: [],
                order: count > 0 ? count : Number.MAX_SAFE_INTEGER,
                label: count > 0 ? pluralFn(count) : unknownLabel
            };
        groups[key].items.push(items[i]);
    }
    return groups;
}

// NOTE: An item matching multiple capabilities appears in multiple groups
// (e.g., an algorithm with both masterCount and splitRatio shows in both).
// This is intentional — capability groups are non-exclusive.
function groupByCapability(items, capGroups, otherLabel) {
    var groups = {};
    for (var g = 0; g < capGroups.length; g++) {
        var cap = capGroups[g];
        groups[cap.key] = { items: [], order: cap.order, label: cap.label };
    }
    for (var i = 0; i < items.length; i++) {
        var placed = false;
        for (var g = 0; g < capGroups.length; g++) {
            if (capGroups[g].test(items[i])) {
                groups[capGroups[g].key].items.push(items[i]);
                placed = true;
            }
        }
        if (!placed) {
            if (!groups["other"])
                groups["other"] = {
                    items: [],
                    order: Number.MAX_SAFE_INTEGER,
                    label: otherLabel
                };
            groups["other"].items.push(items[i]);
        }
    }
    return groups;
}

function ungrouped(items) {
    return { "all": { items: items, order: 0, label: "" } };
}

// ── Sorting ─────────────────────────────────────────────────────────────────

// sortIdx 0 = Name (both modes), 1 = Zone Count (snapping only).
// Tiling sort model only has Name; loadState() clamps persisted values.
function sortItems(groups, sortIdx, ascending) {
    for (var key in groups) {
        groups[key].items.sort(function(a, b) {
            var cmp;
            if (sortIdx === 1) {
                cmp = Math.max(0, a.zoneCount || 0) - Math.max(0, b.zoneCount || 0);
                if (cmp === 0)
                    cmp = (a.name || "").localeCompare(b.name || "");
            } else {
                cmp = (a.name || "").localeCompare(b.name || "");
            }
            return ascending ? cmp : -cmp;
        });
    }
    return groups;
}

function finalizeGroups(groups) {
    var sorted = Object.values(groups).sort(function(a, b) {
        return a.order - b.order;
    });
    var nonEmpty = sorted.filter(function(g) {
        return g.items.length > 0;
    });
    return nonEmpty.map(function(g) {
        return {
            label: nonEmpty.length > 1 ? g.label : "",
            layouts: g.items
        };
    });
}
