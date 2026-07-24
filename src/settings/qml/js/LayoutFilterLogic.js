// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library
.import "GroupSortLogic.js" as Core

/**
 * Layout/algorithm-specific filtering, grouping, and sorting helpers for the
 * layout grid. The neutral, page-agnostic primitives (groupByBoolKey,
 * groupByKeyed, ungrouped, applySort, finalizeGroups) live in GroupSortLogic.js;
 * this file holds only what is specific to layouts and tiling algorithms.
 *
 * Functions receive data and filter config without accessing any QML context.
 * i18n-dependent labels are passed in by the caller.
 *
 * NOTE: sortItems() sorts group arrays in-place for efficiency.
 */

// ── Predicates ──────────────────────────────────────────────────────────────

// `isSystem` is the sole classifier, and the producer emits it for BOTH item
// types (layoutpreviewserialize): ZonesLayoutSource takes it from
// Layout::isSystemLayout(), AutotileLayoutSource from
// !(isScripted && isUserScript). It means "shipped with the app, read-only".
//
// Deliberately NOT `|| item.hasSystemOrigin`. That is a narrower, different
// thing: a USER layout in the user's own directory that shadows a system
// original. It is editable and deletable, so it belongs on the user side of
// this partition — LayoutGridDelegate agrees, badging it "Modified system
// layout" with a document-edit icon rather than the read-only lock. It is also
// added only by the D-Bus enrichment layer (LayoutAdaptor::getLayoutList), so
// reading it here made the built-in/user split flip once the daemon reply
// replaced the local instant-paint list.
function isBuiltIn(item) {
    return item.isSystem === true;
}

function matchesCommonFilters(item, search, showHidden) {
    if (search.length > 0
        && !(item.displayName || "").toLowerCase().includes(search)
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
        if (!f.showPersistent && item.supportsMemory === true)
            return false;
        if (!f.showCustomParams && item.supportsCustomParams === true)
            return false;
        if (!f.showReflowsOnResize && item.reflowsOnResize === true)
            return false;
        if (!f.showScriptState && item.supportsScriptState === true)
            return false;
        if (!f.showSingleWindow && item.supportsSingleWindow === true)
            return false;
        if (!f.showReflowsOnFocus && item.reflowsOnFocus === true)
            return false;
        return true;
    });
}

// ── Grouping ────────────────────────────────────────────────────────────────
// groupByBoolKey / ungrouped live in GroupSortLogic.js (Core). The remaining
// groupers here are layout-specific (aspect ratio, zone count, capability).

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

// ── Sorting ─────────────────────────────────────────────────────────────────

// sortIdx 0 = Name (both modes), 1 = Zone Count, 2 = Priority order.
// Priority order uses the provided customOrder array of IDs (the order set on
// the Configuration → Priority page). Delegates the per-group, asc/desc, stable
// sort machinery to Core.applySort; only the comparator is layout-specific.
function sortItems(groups, sortIdx, ascending, customOrder) {
    // Build index map for custom order (O(1) lookup)
    var orderMap = {};
    var hasCustomOrder = sortIdx === 2 && customOrder && customOrder.length > 0;
    if (hasCustomOrder) {
        for (var i = 0; i < customOrder.length; i++)
            orderMap[customOrder[i]] = i;
    }

    function byName(a, b) {
        return (a.displayName || "").localeCompare(b.displayName || "");
    }

    var comparator;
    if (hasCustomOrder) {
        comparator = function(a, b) {
            var aIdx = (a.id in orderMap) ? orderMap[a.id] : Number.MAX_SAFE_INTEGER;
            var bIdx = (b.id in orderMap) ? orderMap[b.id] : Number.MAX_SAFE_INTEGER;
            return (aIdx !== bIdx) ? aIdx - bIdx : byName(a, b);
        };
    } else if (sortIdx === 1) {
        comparator = function(a, b) {
            var cmp = Math.max(0, a.zoneCount || 0) - Math.max(0, b.zoneCount || 0);
            return cmp !== 0 ? cmp : byName(a, b);
        };
    } else {
        comparator = byName;
    }

    return Core.applySort(groups, comparator, ascending);
}
