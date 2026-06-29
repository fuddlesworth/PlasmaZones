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
        groupByCombo.currentIndex = groupByIndex;
        sortByCombo.currentIndex = sortByIndex;
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
    }
    Component.onCompleted: {
        _previousViewMode = viewMode;
        loadState(viewMode);
    }

    // ── Group By ────────────────────────────────────────────────────────────
    Label {
        text: i18n("Group:")
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.disabledTextColor
    }

    ComboBox {
        id: groupByCombo

        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
        model: root.viewMode === 0 ? root.snappingGroupModel : root.tilingGroupModel
        // Binding is initial-only — user interaction breaks it; loadState()
        // re-syncs imperatively via groupByCombo.currentIndex = ...
        currentIndex: root.groupByIndex
        Accessible.name: i18n("Group by")
        onActivated: index => {
            if (index < 0 || index >= model.length)
                return;

            root.groupByIndex = index;
            root.filterSettingsChanged();
        }
    }

    // ── Sort By ─────────────────────────────────────────────────────────────
    Label {
        text: i18n("Sort:")
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.disabledTextColor
    }

    ComboBox {
        id: sortByCombo

        // Whether a priority order exists for the current mode. hasCustom*Order()
        // is a non-reactive Q_INVOKABLE, so cache it and refresh on the staged-
        // order signals and on the mode switch (model change).
        property bool hasPriorityOrder: false

        function refreshHasPriorityOrder() {
            hasPriorityOrder = root.viewMode === 0 ? settingsController.hasCustomSnappingOrder() : settingsController.hasCustomTilingOrder();
        }

        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
        model: root.viewMode === 0 ? root.snappingSortModel : root.tilingSortModel
        // Binding is initial-only — user interaction breaks it; loadState()
        // re-syncs imperatively via sortByCombo.currentIndex = ...
        currentIndex: root.sortByIndex
        Accessible.name: i18n("Sort by")
        Component.onCompleted: refreshHasPriorityOrder()
        // Fires on the mode switch (the model binding re-evaluates to the other
        // mode's sort array), so the Priority item's enabled state tracks the
        // mode's own order.
        onModelChanged: refreshHasPriorityOrder()
        onActivated: index => {
            if (index < 0 || index >= model.length)
                return;

            // The last option ("Priority") is unavailable until an order is set
            // on the Priority page. Swallow the selection and revert the visible
            // value rather than applying a sort that would silently fall back to
            // Name order.
            if (index === count - 1 && !hasPriorityOrder) {
                currentIndex = root.sortByIndex;
                return;
            }

            root.sortByIndex = index;
            root.filterSettingsChanged();
        }

        // The "Priority" item (the last sort option) is unavailable until an
        // order has been set on the Configuration → Priority page. Keep the
        // delegate ENABLED — a disabled delegate receives no hover events, so the
        // explanatory tooltip would never show. Gray it, suppress the hover
        // highlight, and let onActivated above swallow the selection.
        delegate: ItemDelegate {
            id: sortItemDelegate

            required property int index
            required property var modelData

            readonly property bool isPriority: index === sortByCombo.count - 1
            readonly property bool unavailable: isPriority && !sortByCombo.hasPriorityOrder

            width: sortByCombo.width
            text: modelData
            opacity: unavailable ? 0.5 : 1
            highlighted: !unavailable && sortByCombo.highlightedIndex === index
            ToolTip.visible: hovered && unavailable
            ToolTip.delay: Kirigami.Units.toolTipDelay
            ToolTip.text: i18n("Set a layout order on the Priority page first")
        }

        Connections {
            function onStagedSnappingOrderChanged() {
                sortByCombo.refreshHasPriorityOrder();
            }

            function onStagedTilingOrderChanged() {
                sortByCombo.refreshHasPriorityOrder();
            }

            target: settingsController
        }
    }

    ToolButton {
        icon.name: root.sortAscending ? "view-sort-ascending" : "view-sort-descending"
        icon.width: Kirigami.Units.iconSizes.smallMedium
        icon.height: Kirigami.Units.iconSizes.smallMedium
        onClicked: {
            root.sortAscending = !root.sortAscending;
            root.filterSettingsChanged();
        }
        Accessible.name: root.sortAscending ? i18n("Sort ascending") : i18n("Sort descending")
        ToolTip.visible: hovered
        ToolTip.text: root.sortAscending ? i18n("Ascending") : i18n("Descending")
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
            checked: root.showBuiltInLayouts
        }

        FilterMenuItem {
            text: i18n("User Layouts")
            filterProperty: "showUserLayouts"
            checked: root.showUserLayouts
        }

        MenuSeparator {}

        FilterMenuItem {
            text: i18n("All Monitors")
            filterProperty: "showAspectAny"
            checked: root.showAspectAny
        }

        FilterMenuItem {
            text: i18n("Standard (16:9)")
            filterProperty: "showAspectStandard"
            checked: root.showAspectStandard
        }

        FilterMenuItem {
            text: i18n("Ultrawide (21:9)")
            filterProperty: "showAspectUltrawide"
            checked: root.showAspectUltrawide
        }

        FilterMenuItem {
            text: i18n("Super-Ultrawide (32:9)")
            filterProperty: "showAspectSuperUltrawide"
            checked: root.showAspectSuperUltrawide
        }

        FilterMenuItem {
            text: i18n("Portrait (9:16)")
            filterProperty: "showAspectPortrait"
            checked: root.showAspectPortrait
        }

        MenuSeparator {}

        FilterMenuItem {
            text: i18n("Auto")
            filterProperty: "showAutoLayouts"
            checked: root.showAutoLayouts
        }

        FilterMenuItem {
            text: i18n("Manual")
            filterProperty: "showManualLayouts"
            checked: root.showManualLayouts
        }

        MenuSeparator {}

        FilterMenuItem {
            text: i18n("Show Hidden Layouts")
            filterProperty: "showHidden"
            checked: root.showHidden
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
            checked: root.showBuiltInAlgorithms
        }

        FilterMenuItem {
            text: i18n("User Scripts")
            filterProperty: "showUserAlgorithms"
            checked: root.showUserAlgorithms
        }

        MenuSeparator {}

        FilterMenuItem {
            text: i18n("Master Count")
            filterProperty: "showMasterCount"
            checked: root.showMasterCount
        }

        FilterMenuItem {
            text: i18n("Split Ratio")
            filterProperty: "showSplitRatio"
            checked: root.showSplitRatio
        }

        FilterMenuItem {
            text: i18n("Overlapping Zones")
            filterProperty: "showOverlapping"
            checked: root.showOverlapping
        }

        FilterMenuItem {
            text: i18n("Persistent (Memory)")
            filterProperty: "showPersistent"
            checked: root.showPersistent
        }

        FilterMenuItem {
            text: i18n("Custom Parameters")
            filterProperty: "showCustomParams"
            checked: root.showCustomParams
        }

        FilterMenuItem {
            text: i18n("Reflows")
            filterProperty: "showReflowsOnResize"
            checked: root.showReflowsOnResize
        }

        FilterMenuItem {
            text: i18n("Script State")
            filterProperty: "showScriptState"
            checked: root.showScriptState
        }

        FilterMenuItem {
            text: i18n("Single Window")
            filterProperty: "showSingleWindow"
            checked: root.showSingleWindow
        }

        FilterMenuItem {
            text: i18n("Follows Focus")
            filterProperty: "showReflowsOnFocus"
            checked: root.showReflowsOnFocus
        }

        MenuSeparator {}

        FilterMenuItem {
            text: i18n("Show Hidden Algorithms")
            filterProperty: "showHidden"
            checked: root.showHidden
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

    // Checkable menu item that writes back to a named root filter property.
    // Uses an explicit Binding so the checked state survives imperative
    // toggles (checkable breaks declarative bindings on user click).
    component FilterMenuItem: MenuItem {
        required property string filterProperty

        checkable: true
        onToggled: {
            if (root._resetting)
                return;

            root[filterProperty] = checked;
            root.filterSettingsChanged();
        }

        Binding on checked {
            value: root[filterProperty]
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
