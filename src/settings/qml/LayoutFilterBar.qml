// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtCore
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Filter bar for layout/algorithm grid — group by, sort by, filters, and text search.
 *
 * Options change dynamically based on viewMode (0 = Snapping, 1 = Tiling).
 */
RowLayout {
    id: root

    property int viewMode: 0
    // ── Exposed state: group / sort ─────────────────────────────────────────
    property int groupByIndex: 0
    property int sortByIndex: 0
    property bool sortAscending: true
    property string filterText: ""
    // ── Exposed state: shared filters ───────────────────────────────────────
    // Default ON: the layouts page shows hidden (curated-out) items by default
    // so users can see and re-enable them; unchecking hides them.
    property bool showHidden: true
    // ── Exposed state: snapping filters (all ON = show everything) ──────────
    property bool showAspectAny: true
    property bool showAspectStandard: true
    property bool showAspectUltrawide: true
    property bool showAspectSuperUltrawide: true
    property bool showAspectPortrait: true
    property bool showAutoLayouts: true
    property bool showManualLayouts: true
    property bool showBuiltInLayouts: true
    property bool showUserLayouts: true
    // ── Exposed state: tiling filters ───────────────────────────────────────
    property bool showBuiltInAlgorithms: true
    property bool showUserAlgorithms: true
    property bool showMasterCount: true
    property bool showSplitRatio: true
    property bool showOverlapping: true
    property bool showPersistent: true
    property bool showCustomParams: true
    property bool showReflowsOnResize: true
    property bool showScriptState: true
    property bool showSingleWindow: true
    property bool showReflowsOnFocus: true
    // Whether any non-default filter is active (drives badge visibility).
    // Derived from the active mode's filter button rather than a hand-written
    // list: its `excluded` already tracks every bool filter that is off, via
    // _excludedKeys over the button's own group entries.
    readonly property bool hasActiveFilters: filterText.length > 0 || (viewMode === 0 ? snappingFilterButton : tilingFilterButton).excluded.length > 0
    // ── Group-by index constants (must match model order below) ───────────
    // Snapping
    readonly property int groupAspectRatio: 0
    readonly property int groupZoneCount: 1
    readonly property int groupAutoManual: 2
    readonly property int groupSource: 3
    readonly property int groupVisibility: 4
    readonly property int groupSnappingNone: 5
    // Tiling. "Persistent" was dropped — it was a degenerate yes/no split already
    // covered by the Capability grouping's "Persistent (Memory)" bucket.
    readonly property int groupCapability: 0
    readonly property int groupTilingSource: 1
    readonly property int groupTilingVisibility: 2
    readonly property int groupTilingNone: 3
    // Static ComboBox models (avoids inline array recreation that resets currentIndex)
    readonly property var snappingGroupModel: [i18n("Aspect Ratio"), i18n("Zone Count"), i18n("Auto / Manual"), i18n("Source"), i18n("Visibility"), i18n("None")]
    readonly property var tilingGroupModel: [i18n("Capability"), i18n("Source"), i18n("Visibility"), i18n("None")]
    // "Priority" sorts by the order set on the Configuration → Priority page
    // (snappingLayoutOrder / tilingAlgorithmOrder). Falls back to Name order when
    // no priority has been set yet.
    readonly property var snappingSortModel: [i18n("Name"), i18n("Zone Count"), i18n("Priority")]
    readonly property var tilingSortModel: [i18n("Name"), i18n("Zone Count"), i18n("Priority")]
    // Guard to suppress redundant filterSettingsChanged during batch resets
    property bool _resetting: false
    property int _previousViewMode: 0
    // Cached "a priority order exists for the current mode" — hasCustom*Order()
    // is a non-reactive Q_INVOKABLE, so refresh it on completion, mode switch,
    // and the staged-order signals. Drives the Priority sort option's availability
    // in the GroupSortBar (its last sort entry).
    property bool _hasPriorityOrder: false

    function _refreshHasPriorityOrder() {
        _hasPriorityOrder = viewMode === 0 ? settingsController.hasCustomSnappingOrder() : settingsController.hasCustomTilingOrder();
    }
    // Property-name maps for data-driven save/load.
    // Each entry: [rootPropertyName, persistedStatePropertyName].
    // NOTE: adding a filter requires updating ALL of: property declaration,
    // _defaultValues, the relevant _snapping/_tilingStateMap, persistedState,
    // the filter button's group entry, and the JS filter logic (see the same
    // note above _defaultValues below — keep both lists in sync).
    // hasActiveFilters needs no update: it derives from the button's excluded.
    readonly property var _snappingStateMap: [["groupByIndex", "snappingGroupByIndex"], ["sortByIndex", "snappingSortByIndex"], ["sortAscending", "snappingSortAscending"], ["showHidden", "snappingShowHidden"], ["showAspectAny", "snappingShowAspectAny"], ["showAspectStandard", "snappingShowAspectStandard"], ["showAspectUltrawide", "snappingShowAspectUltrawide"], ["showAspectSuperUltrawide", "snappingShowAspectSuperUltrawide"], ["showAspectPortrait", "snappingShowAspectPortrait"], ["showAutoLayouts", "snappingShowAutoLayouts"], ["showManualLayouts", "snappingShowManualLayouts"], ["showBuiltInLayouts", "snappingShowBuiltInLayouts"], ["showUserLayouts", "snappingShowUserLayouts"]]
    readonly property var _tilingStateMap: [["groupByIndex", "tilingGroupByIndex"], ["sortByIndex", "tilingSortByIndex"], ["sortAscending", "tilingSortAscending"], ["showHidden", "tilingShowHidden"], ["showBuiltInAlgorithms", "tilingShowBuiltInAlgorithms"], ["showUserAlgorithms", "tilingShowUserAlgorithms"], ["showMasterCount", "tilingShowMasterCount"], ["showSplitRatio", "tilingShowSplitRatio"], ["showOverlapping", "tilingShowOverlapping"], ["showPersistent", "tilingShowPersistent"], ["showCustomParams", "tilingShowCustomParams"], ["showReflowsOnResize", "tilingShowReflowsOnResize"], ["showScriptState", "tilingShowScriptState"], ["showSingleWindow", "tilingShowSingleWindow"], ["showReflowsOnFocus", "tilingShowReflowsOnFocus"]]
    // Default values for all resettable filter properties (not group/sort).
    // Adding a filter requires updating: property declaration, _defaultValues,
    // the relevant state map, persistedState, the filter button's group entry,
    // and the JS logic.
    readonly property var _defaultValues: {
        "showHidden": true,
        "showAspectAny": true,
        "showAspectStandard": true,
        "showAspectUltrawide": true,
        "showAspectSuperUltrawide": true,
        "showAspectPortrait": true,
        "showAutoLayouts": true,
        "showManualLayouts": true,
        "showBuiltInLayouts": true,
        "showUserLayouts": true,
        "showBuiltInAlgorithms": true,
        "showUserAlgorithms": true,
        "showMasterCount": true,
        "showSplitRatio": true,
        "showOverlapping": true,
        "showPersistent": true,
        "showCustomParams": true,
        "showReflowsOnResize": true,
        "showScriptState": true,
        "showSingleWindow": true,
        "showReflowsOnFocus": true
    }

    signal filterSettingsChanged
    // Emitted when search text is programmatically cleared (reset / view
    // switch) so the page's search field — which now lives outside this
    // component — can clear itself.
    signal searchCleared

    // Apply the page's search field text through the debounce. Called from the
    // hosting page's SearchField since the field moved out to the search row.
    function setSearchText(text) {
        if (_resetting)
            return;
        searchDebounce.pendingText = text;
        searchDebounce.restart();
    }

    // Open the filter checkbox menu for the current view mode. Invoked by the
    // page's filter button (the button moved to the search row).
    function popupFilterMenu() {
        if (viewMode === 0)
            snappingFilterButton.popup();
        else
            tilingFilterButton.popup();
    }

    // Keys the given filter button should show as unchecked: every bool
    // filter property (the group entries' keys ARE the property names) that
    // is currently false. Evaluated as a `Binding on excluded` value, so the
    // per-property reads make it re-evaluate whenever any filter bool
    // changes (reset, loadState, or a toggle echoing back).
    function _excludedKeys(groups) {
        var ex = [];
        for (var g = 0; g < groups.length; ++g) {
            for (var i = 0; i < groups[g].length; ++i) {
                var key = groups[g][i].key;
                if (!root[key])
                    ex.push(key);
            }
        }
        return ex;
    }

    // A checkbox in one of the filter menus was toggled: write it onto the
    // named bool property (which flows to persistedState via
    // filterSettingsChanged → saveState, exactly as before).
    function _applyFilterToggle(key, included) {
        if (_resetting)
            return;

        root[key] = included;
        filterSettingsChanged();
    }

    // Resets filter and search state to defaults.
    // Group-by and sort-by are intentionally preserved — they are visible in
    // the toolbar and easy to change directly; "Reset Filters" targets the
    // hidden dropdown state only.
    function resetFilters() {
        _resetting = true;
        searchDebounce.stop();
        searchCleared();
        filterText = "";
        // Only reset properties relevant to the current view mode
        let map = viewMode === 0 ? _snappingStateMap : _tilingStateMap;
        for (let i = 0; i < map.length; i++) {
            let prop = map[i][0];
            if (prop in _defaultValues)
                root[prop] = _defaultValues[prop];
        }
        _resetting = false;
        filterSettingsChanged();
    }

    function saveState(mode) {
        let map = mode === 0 ? _snappingStateMap : _tilingStateMap;
        for (let i = 0; i < map.length; i++)
            persistedState[map[i][1]] = root[map[i][0]];
    }

    function loadState(mode) {
        _resetting = true;
        // filterText is intentionally not persisted — always start with empty search
        searchDebounce.stop();
        searchCleared();
        filterText = "";
        let map = mode === 0 ? _snappingStateMap : _tilingStateMap;
        let maxGroup = (mode === 0 ? snappingGroupModel : tilingGroupModel).length - 1;
        let maxSort = (mode === 0 ? snappingSortModel : tilingSortModel).length - 1;
        for (let i = 0; i < map.length; i++) {
            let prop = map[i][0];
            let val = persistedState[map[i][1]];
            if (prop === "groupByIndex")
                val = Math.max(0, Math.min(val, maxGroup));
            else if (prop === "sortByIndex")
                val = Math.max(0, Math.min(val, maxSort));
            root[prop] = val;
        }
        groupSort.groupByIndex = groupByIndex;
        groupSort.sortByIndex = sortByIndex;
        groupSort.sortAscending = sortAscending;
        groupSort.syncFromState();
        _resetting = false;
        filterSettingsChanged();
    }

    onFilterSettingsChanged: {
        if (!_resetting)
            saveState(viewMode);
    }
    spacing: Kirigami.Units.smallSpacing
    // Save current mode state, then load the new mode's persisted state.
    // Guard: skip if called during _resetting to avoid saving partial state.
    onViewModeChanged: {
        if (_resetting)
            return;

        saveState(_previousViewMode);
        _previousViewMode = viewMode;
        loadState(viewMode);
        // The new mode has its own priority order — refresh the Priority sort
        // option's availability for the mode we just switched to.
        _refreshHasPriorityOrder();
    }
    Component.onCompleted: {
        _previousViewMode = viewMode;
        loadState(viewMode);
        _refreshHasPriorityOrder();
    }

    // Priority order can be staged on the Configuration → Priority page while
    // this bar is alive — refresh the cached availability so the Priority sort
    // option ungreys without a mode switch. The order itself is read
    // imperatively by the host's rebuildModel (effectiveSnappingOrder /
    // effectiveTilingOrder), which only reruns on filterSettingsChanged, so
    // emit that too or a staged reorder leaves the cards in the stale order
    // while Priority sort is active. Not folded into _refreshHasPriorityOrder:
    // its other two callers (Component.onCompleted, onViewModeChanged) already
    // sit next to a loadState() that emits, and would rebuild twice.
    Connections {
        function onStagedSnappingOrderChanged() {
            root._refreshHasPriorityOrder();
            root.filterSettingsChanged();
        }

        function onStagedTilingOrderChanged() {
            root._refreshHasPriorityOrder();
            root.filterSettingsChanged();
        }

        target: settingsController
    }

    // ── Group / Sort / direction ─────────────────────────────────────────────
    // The shared GroupSortBar owns the combos + direction toggle; this bar keeps
    // ownership of the persisted state (per view mode) and pushes it in via
    // syncFromState() on load / mode switch, pulling user edits back out in
    // onChanged. "Priority" (the last sort option) is unavailable until an order
    // is set on the Configuration → Priority page; _hasPriorityOrder tracks that.
    GroupSortBar {
        id: groupSort

        groupModel: root.viewMode === 0 ? root.snappingGroupModel : root.tilingGroupModel
        sortModel: root.viewMode === 0 ? root.snappingSortModel : root.tilingSortModel
        sortItemAvailable: [true, true, root._hasPriorityOrder]
        disabledSortTooltip: i18n("Set a layout order on the Priority page first")
        onChanged: {
            root.groupByIndex = groupByIndex;
            root.sortByIndex = sortByIndex;
            root.sortAscending = sortAscending;
            root.filterSettingsChanged();
        }
    }

    Timer {
        id: searchDebounce

        property string pendingText: ""

        interval: 150
        onTriggered: {
            root.filterText = pendingText;
            root.filterSettingsChanged();
        }
    }

    // ── Filter menus ────────────────────────────────────────────────────────
    // Both checkbox filter menus are FilterMenuButton instances hosted
    // invisibly: this bar keeps ownership of the bool filter properties and
    // their QtCore.Settings persistence, and the page's own toolbar button
    // opens the menu via popupFilterMenu(), so the ToolButton chrome is
    // unused. Group entries use the bool property names as keys; `excluded`
    // is derived from those properties through a `Binding on` value source
    // (the button reassigns `excluded` internally on toggle, a JS write that
    // would sever a plain binding), and user edits come back via
    // filterToggled / resetTriggered.
    FilterMenuButton {
        id: snappingFilterButton

        visible: false
        menuTitle: i18n("Filter Layouts")
        externalFilterActive: root.filterText.length > 0
        groups: [[
                {
                    "key": "showBuiltInLayouts",
                    "label": i18n("Built-in")
                },
                {
                    "key": "showUserLayouts",
                    "label": i18n("User Layouts")
                }
            ], [
                {
                    "key": "showAspectAny",
                    "label": i18n("All Monitors")
                },
                {
                    "key": "showAspectStandard",
                    "label": i18n("Standard (16:9)")
                },
                {
                    "key": "showAspectUltrawide",
                    "label": i18n("Ultrawide (21:9)")
                },
                {
                    "key": "showAspectSuperUltrawide",
                    "label": i18n("Super-Ultrawide (32:9)")
                },
                {
                    "key": "showAspectPortrait",
                    "label": i18n("Portrait (9:16)")
                }
            ], [
                {
                    "key": "showAutoLayouts",
                    "label": i18n("Auto")
                },
                {
                    "key": "showManualLayouts",
                    "label": i18n("Manual")
                }
            ], [
                {
                    "key": "showHidden",
                    "label": i18n("Show Hidden Layouts")
                }
            ]]
        onFilterToggled: (key, included) => root._applyFilterToggle(key, included)
        onResetTriggered: root.resetFilters()

        Binding on excluded {
            value: root._excludedKeys(snappingFilterButton.groups)
            restoreMode: Binding.RestoreNone
        }
    }

    // ── Tiling Filter Menu ──────────────────────────────────────────────────
    FilterMenuButton {
        id: tilingFilterButton

        visible: false
        menuTitle: i18n("Filter Algorithms")
        externalFilterActive: root.filterText.length > 0
        groups: [[
                {
                    "key": "showBuiltInAlgorithms",
                    "label": i18n("Built-in")
                },
                {
                    "key": "showUserAlgorithms",
                    "label": i18n("User Scripts")
                }
            ], [
                {
                    "key": "showMasterCount",
                    "label": i18n("Master Count")
                },
                {
                    "key": "showSplitRatio",
                    "label": i18n("Split Ratio")
                },
                {
                    "key": "showOverlapping",
                    "label": i18n("Overlapping Zones")
                },
                {
                    "key": "showPersistent",
                    "label": i18n("Persistent (Memory)")
                },
                {
                    "key": "showCustomParams",
                    "label": i18n("Custom Parameters")
                },
                {
                    "key": "showReflowsOnResize",
                    "label": i18n("Reflows")
                },
                {
                    "key": "showScriptState",
                    "label": i18n("Script State")
                },
                {
                    "key": "showSingleWindow",
                    "label": i18n("Single Window")
                },
                {
                    "key": "showReflowsOnFocus",
                    "label": i18n("Follows Focus")
                }
            ], [
                {
                    "key": "showHidden",
                    "label": i18n("Show Hidden Algorithms")
                }
            ]]
        onFilterToggled: (key, included) => root._applyFilterToggle(key, included)
        onResetTriggered: root.resetFilters()

        Binding on excluded {
            value: root._excludedKeys(tilingFilterButton.groups)
            restoreMode: Binding.RestoreNone
        }
    }

    // ── Persisted UI state (per view mode) ───────────────────────────────
    Settings {
        id: persistedState

        // Snapping mode
        property int snappingGroupByIndex: 0
        property int snappingSortByIndex: 0
        property bool snappingSortAscending: true
        property bool snappingShowHidden: true
        property bool snappingShowAspectAny: true
        property bool snappingShowAspectStandard: true
        property bool snappingShowAspectUltrawide: true
        property bool snappingShowAspectSuperUltrawide: true
        property bool snappingShowAspectPortrait: true
        property bool snappingShowAutoLayouts: true
        property bool snappingShowManualLayouts: true
        property bool snappingShowBuiltInLayouts: true
        property bool snappingShowUserLayouts: true
        // Tiling mode
        property int tilingGroupByIndex: 0
        property int tilingSortByIndex: 0
        property bool tilingSortAscending: true
        property bool tilingShowHidden: true
        property bool tilingShowBuiltInAlgorithms: true
        property bool tilingShowUserAlgorithms: true
        property bool tilingShowMasterCount: true
        property bool tilingShowSplitRatio: true
        property bool tilingShowOverlapping: true
        property bool tilingShowPersistent: true
        property bool tilingShowCustomParams: true
        property bool tilingShowReflowsOnResize: true
        property bool tilingShowScriptState: true
        property bool tilingShowSingleWindow: true
        property bool tilingShowReflowsOnFocus: true

        category: "LayoutsPageFilterBar"
    }
}
