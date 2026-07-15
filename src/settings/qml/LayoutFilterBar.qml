// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtCore
import QtQuick
import QtQuick.Controls
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
    // Whether any non-default filter is active (drives badge visibility)
    readonly property bool hasActiveFilters: {
        if (filterText.length > 0)
            return true;

        if (root.viewMode === 0)
            return !showAspectAny || !showAspectStandard || !showAspectUltrawide || !showAspectSuperUltrawide || !showAspectPortrait || !showHidden || !showAutoLayouts || !showManualLayouts || !showBuiltInLayouts || !showUserLayouts;
        else
            return !showBuiltInAlgorithms || !showUserAlgorithms || !showHidden || !showMasterCount || !showSplitRatio || !showOverlapping || !showPersistent || !showCustomParams || !showReflowsOnResize || !showScriptState || !showSingleWindow || !showReflowsOnFocus;
    }
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
    // hasActiveFilters, the menu item, and the JS filter logic (see the same
    // note above _defaultValues below — keep both lists in sync).
    readonly property var _snappingStateMap: [["groupByIndex", "snappingGroupByIndex"], ["sortByIndex", "snappingSortByIndex"], ["sortAscending", "snappingSortAscending"], ["showHidden", "snappingShowHidden"], ["showAspectAny", "snappingShowAspectAny"], ["showAspectStandard", "snappingShowAspectStandard"], ["showAspectUltrawide", "snappingShowAspectUltrawide"], ["showAspectSuperUltrawide", "snappingShowAspectSuperUltrawide"], ["showAspectPortrait", "snappingShowAspectPortrait"], ["showAutoLayouts", "snappingShowAutoLayouts"], ["showManualLayouts", "snappingShowManualLayouts"], ["showBuiltInLayouts", "snappingShowBuiltInLayouts"], ["showUserLayouts", "snappingShowUserLayouts"]]
    readonly property var _tilingStateMap: [["groupByIndex", "tilingGroupByIndex"], ["sortByIndex", "tilingSortByIndex"], ["sortAscending", "tilingSortAscending"], ["showHidden", "tilingShowHidden"], ["showBuiltInAlgorithms", "tilingShowBuiltInAlgorithms"], ["showUserAlgorithms", "tilingShowUserAlgorithms"], ["showMasterCount", "tilingShowMasterCount"], ["showSplitRatio", "tilingShowSplitRatio"], ["showOverlapping", "tilingShowOverlapping"], ["showPersistent", "tilingShowPersistent"], ["showCustomParams", "tilingShowCustomParams"], ["showReflowsOnResize", "tilingShowReflowsOnResize"], ["showScriptState", "tilingShowScriptState"], ["showSingleWindow", "tilingShowSingleWindow"], ["showReflowsOnFocus", "tilingShowReflowsOnFocus"]]
    // Default values for all resettable filter properties (not group/sort).
    // Adding a filter requires updating: property declaration, _defaultValues,
    // the relevant state map, persistedState, hasActiveFilters, menu item, and JS logic.
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
            snappingFilterMenu.popup();
        else
            tilingFilterMenu.popup();
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
    // option ungreys without a mode switch.
    Connections {
        function onStagedSnappingOrderChanged() {
            root._refreshHasPriorityOrder();
        }

        function onStagedTilingOrderChanged() {
            root._refreshHasPriorityOrder();
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

    Menu {
        id: snappingFilterMenu

        title: i18n("Filter Layouts")

        FilterMenuItem {
            text: i18n("Built-in")
            filterProperty: "showBuiltInLayouts"
        }

        FilterMenuItem {
            text: i18n("User Layouts")
            filterProperty: "showUserLayouts"
        }

        MenuSeparator {}

        FilterMenuItem {
            text: i18n("All Monitors")
            filterProperty: "showAspectAny"
        }

        FilterMenuItem {
            text: i18n("Standard (16:9)")
            filterProperty: "showAspectStandard"
        }

        FilterMenuItem {
            text: i18n("Ultrawide (21:9)")
            filterProperty: "showAspectUltrawide"
        }

        FilterMenuItem {
            text: i18n("Super-Ultrawide (32:9)")
            filterProperty: "showAspectSuperUltrawide"
        }

        FilterMenuItem {
            text: i18n("Portrait (9:16)")
            filterProperty: "showAspectPortrait"
        }

        MenuSeparator {}

        FilterMenuItem {
            text: i18n("Auto")
            filterProperty: "showAutoLayouts"
        }

        FilterMenuItem {
            text: i18n("Manual")
            filterProperty: "showManualLayouts"
        }

        MenuSeparator {}

        FilterMenuItem {
            text: i18n("Show Hidden Layouts")
            filterProperty: "showHidden"
        }

        MenuSeparator {}

        ResetMenuItem {}
    }

    // ── Tiling Filter Menu ──────────────────────────────────────────────────
    Menu {
        id: tilingFilterMenu

        title: i18n("Filter Algorithms")

        FilterMenuItem {
            text: i18n("Built-in")
            filterProperty: "showBuiltInAlgorithms"
        }

        FilterMenuItem {
            text: i18n("User Scripts")
            filterProperty: "showUserAlgorithms"
        }

        MenuSeparator {}

        FilterMenuItem {
            text: i18n("Master Count")
            filterProperty: "showMasterCount"
        }

        FilterMenuItem {
            text: i18n("Split Ratio")
            filterProperty: "showSplitRatio"
        }

        FilterMenuItem {
            text: i18n("Overlapping Zones")
            filterProperty: "showOverlapping"
        }

        FilterMenuItem {
            text: i18n("Persistent (Memory)")
            filterProperty: "showPersistent"
        }

        FilterMenuItem {
            text: i18n("Custom Parameters")
            filterProperty: "showCustomParams"
        }

        FilterMenuItem {
            text: i18n("Reflows")
            filterProperty: "showReflowsOnResize"
        }

        FilterMenuItem {
            text: i18n("Script State")
            filterProperty: "showScriptState"
        }

        FilterMenuItem {
            text: i18n("Single Window")
            filterProperty: "showSingleWindow"
        }

        FilterMenuItem {
            text: i18n("Follows Focus")
            filterProperty: "showReflowsOnFocus"
        }

        MenuSeparator {}

        FilterMenuItem {
            text: i18n("Show Hidden Algorithms")
            filterProperty: "showHidden"
        }

        MenuSeparator {}

        ResetMenuItem {}
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

    // Checkable menu item that writes back to a named root filter property
    // and keeps the menu open on toggle (StayOpenMenuItem). An explicit
    // onToggled drives the filter property and a binding reads it back, so a
    // programmatic reset lands on the item.
    //
    // `Binding on checked` rather than a plain `checked:` binding, and the
    // difference is load-bearing here. What StayOpenMenuItem has to avoid is
    // AbstractButton's activation path, which emits triggered() and dismisses
    // the menu; the way it does that is to flip `checked` with a JS assignment
    // and re-emit toggled() by hand. A JS write severs a plain binding for
    // good, and `Binding on` is a value source that survives it. Without it,
    // Reset Filters would stop reaching any item the user had clicked.
    //
    // RestoreNone is stated rather than inherited: these bindings carry no
    // `when` and so never deactivate, and a delegate being torn down has
    // nothing worth restoring onto.
    component FilterMenuItem: StayOpenMenuItem {
        required property string filterProperty

        onToggled: {
            if (root._resetting)
                return;

            root[filterProperty] = checked;
            root.filterSettingsChanged();
        }

        Binding on checked {
            value: root[filterProperty]
            restoreMode: Binding.RestoreNone
        }
    }

    // Shared "Reset Filters" action used by both filter menus
    component ResetMenuItem: MenuItem {
        text: i18n("Reset Filters")
        icon.name: "edit-reset"
        enabled: root.hasActiveFilters
        onTriggered: root.resetFilters()
    }
}
