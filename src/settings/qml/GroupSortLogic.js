// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library

/**
 * Neutral grouping / sorting primitives for the settings listing pages.
 * Functions take plain data and caller-supplied, i18n-resolved labels and never
 * touch any QML context, so they're page-agnostic. The Layouts page (snapping
 * layouts + tiling algorithms) is the current consumer; the primitives are kept
 * here as a small reusable library for any future grouped/sorted listing.
 *
 * Group shape: an object keyed by group id, each value `{ items, order, label }`.
 * `order` drives inter-group ordering; `label` is the (already localized) header.
 *
 * NOTE: applySort() sorts the per-group item arrays in place for efficiency.
 */

// ── Grouping ────────────────────────────────────────────────────────────────

// Two-bucket boolean split. `testFn(item)` true → trueKey bucket (order 0),
// false → falseKey bucket (order 1).
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

// General N-bucket grouping — the multi-bucket generalization of
// groupByBoolKey, provided by the library for reuse (no page consumes it today;
// the Layouts page only needs the two-bucket split). `keyFn(item)` returns
// `{ key, order, label }` describing the bucket the item belongs to. Items
// mapping to the same `key` share a bucket; the first occurrence's
// `order`/`label` win.
function groupByKeyed(items, keyFn) {
    var groups = {};
    for (var i = 0; i < items.length; i++) {
        var d = keyFn(items[i]);
        var key = d.key;
        if (!groups[key])
            groups[key] = {
                items: [],
                order: d.order,
                label: d.label
            };
        groups[key].items.push(items[i]);
    }
    return groups;
}

// Single bucket — used by the "None" grouping option. `label` is the (already
// localized) header the caller wants on the lone card (e.g. "All layouts");
// pass "" / omit for a header-less card. finalizeGroups still drops an empty
// label, so the card only renders a header when a real label is supplied.
function ungrouped(items, label) {
    return { "all": { items: items, order: 0, label: label || "" } };
}

// ── Sorting ─────────────────────────────────────────────────────────────────

// Sort every group's items in place with `comparator(a, b)` (ascending sense).
// `ascending === false` negates the result. Qt's V4 sort is stable, so equal
// keys preserve input order (e.g. the evaluator's own priority tie-break).
function applySort(groups, comparator, ascending) {
    for (var key in groups) {
        groups[key].items.sort(function(a, b) {
            var cmp = comparator(a, b);
            return ascending ? cmp : -cmp;
        });
    }
    return groups;
}

// ── Finalization ─────────────────────────────────────────────────────────────

// Flatten the group map into an ordered, non-empty `[{ label, items }]` array.
// When only one non-empty group remains its header is dropped — UNLESS
// `keepSingleLabel` is true. The Layouts page passes true for its "None"
// grouping (so the lone "All layouts" card keeps its header) and false
// otherwise (so a real grouping that happens to collapse to one group renders
// header-less).
function finalizeGroups(groups, keepSingleLabel) {
    var sorted = Object.values(groups).sort(function(a, b) {
        return a.order - b.order;
    });
    var nonEmpty = sorted.filter(function(g) {
        return g.items.length > 0;
    });
    var showLabel = keepSingleLabel === true || nonEmpty.length > 1;
    return nonEmpty.map(function(g) {
        return {
            label: showLabel ? g.label : "",
            items: g.items
        };
    });
}
