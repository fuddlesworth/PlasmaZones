// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

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
    property bool showHidden: false
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
    // Whether any non-default filter is active (drives badge visibility)
    readonly property bool hasActiveFilters: {
        if (filterText.length > 0)
            return true;

        if (root.viewMode === 0)
            return !showAspectAny || !showAspectStandard || !showAspectUltrawide || !showAspectSuperUltrawide || !showAspectPortrait || showHidden || !showAutoLayouts || !showManualLayouts || !showBuiltInLayouts || !showUserLayouts;
        else
            return !showBuiltInAlgorithms || !showUserAlgorithms || showHidden || !showMasterCount || !showSplitRatio || !showOverlapping || !showPersistent;
    }
    // Static ComboBox models (avoids inline array recreation that resets currentIndex)
    readonly property var snappingGroupModel: [i18n("Aspect Ratio"), i18n("Zone Count"), i18n("Auto / Manual"), i18n("Source"), i18n("None")]
    readonly property var tilingGroupModel: [i18n("Capability"), i18n("Source"), i18n("Persistent"), i18n("None")]
    readonly property var sortModel: [i18n("Name"), i18n("Zone Count")]
    // Guard to suppress redundant filterSettingsChanged during batch resets
    property bool _resetting: false

    signal filterSettingsChanged()

    // Resets filter and search state to defaults.
    // Group-by and sort-by are intentionally preserved — they are visible in
    // the toolbar and easy to change directly; "Reset Filters" targets the
    // hidden dropdown state only.
    function resetFilters() {
        _resetting = true;
        searchField.clear();
        filterText = "";
        showHidden = false;
        showAspectAny = true;
        showAspectStandard = true;
        showAspectUltrawide = true;
        showAspectSuperUltrawide = true;
        showAspectPortrait = true;
        showAutoLayouts = true;
        showManualLayouts = true;
        showBuiltInLayouts = true;
        showUserLayouts = true;
        showBuiltInAlgorithms = true;
        showUserAlgorithms = true;
        showMasterCount = true;
        showSplitRatio = true;
        showOverlapping = true;
        showPersistent = true;
        _resetting = false;
    }

    spacing: Kirigami.Units.smallSpacing
    // Reset all state when view mode changes
    onViewModeChanged: {
        root.groupByIndex = 0;
        root.sortByIndex = 0;
        root.sortAscending = true;
        resetFilters();
        groupByCombo.currentIndex = 0;
        sortByCombo.currentIndex = 0;
        root.filterSettingsChanged();
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
        currentIndex: root.groupByIndex
        onActivated: (index) => {
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

        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
        model: root.sortModel
        currentIndex: root.sortByIndex
        onActivated: (index) => {
            root.sortByIndex = index;
            root.filterSettingsChanged();
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

    Item {
        Layout.fillWidth: true
    }

    // ── Search ──────────────────────────────────────────────────────────────
    TextField {
        id: searchField

        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
        placeholderText: root.viewMode === 0 ? i18n("Search layouts\u2026") : i18n("Search algorithms\u2026")
        inputMethodHints: Qt.ImhNoPredictiveText
        rightPadding: clearButton.visible ? clearButton.width + Kirigami.Units.smallSpacing : undefined
        onTextChanged: {
            root.filterText = text;
            if (!root._resetting)
                root.filterSettingsChanged();

        }

        ToolButton {
            id: clearButton

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: searchField.text.length > 0
            icon.name: "edit-clear"
            icon.width: Kirigami.Units.iconSizes.small
            icon.height: Kirigami.Units.iconSizes.small
            onClicked: searchField.clear()
            Accessible.name: i18n("Clear search")
        }

    }

    // ── Filter Button ───────────────────────────────────────────────────────
    // checked is driven by binding, not user toggle — checkable intentionally omitted
    ToolButton {
        id: filterButton

        icon.name: "view-filter"
        checked: root.hasActiveFilters
        onClicked: {
            if (root.viewMode === 0)
                snappingFilterMenu.popup();
            else
                tilingFilterMenu.popup();
        }
        Accessible.name: root.hasActiveFilters ? i18n("Filter (active)") : i18n("Filter")
        ToolTip.visible: hovered
        ToolTip.text: root.hasActiveFilters ? i18n("Filters active \u2014 click to change") : i18n("Filter")
    }

    // ── Snapping Filter Menu ────────────────────────────────────────────────
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

        MenuSeparator {
        }

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

        MenuSeparator {
        }

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

        MenuSeparator {
        }

        FilterMenuItem {
            text: i18n("Show Hidden Layouts")
            filterProperty: "showHidden"
            checked: root.showHidden
        }

        MenuSeparator {
        }

        ResetMenuItem {
        }

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

        MenuSeparator {
        }

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

        MenuSeparator {
        }

        FilterMenuItem {
            text: i18n("Show Hidden Algorithms")
            filterProperty: "showHidden"
            checked: root.showHidden
        }

        MenuSeparator {
        }

        ResetMenuItem {
        }

    }

    // Checkable menu item that writes back to a named root filter property
    component FilterMenuItem: MenuItem {
        required property string filterProperty

        checkable: true
        onToggled: {
            root[filterProperty] = checked;
            root.filterSettingsChanged();
        }
    }

    // Shared "Reset Filters" action used by both filter menus
    component ResetMenuItem: MenuItem {
        text: i18n("Reset Filters")
        icon.name: "edit-reset"
        enabled: root.hasActiveFilters
        onTriggered: {
            root.resetFilters();
            root.filterSettingsChanged();
        }
    }

}
