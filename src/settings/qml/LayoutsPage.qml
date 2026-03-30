// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import "LayoutFilterLogic.js" as Logic
import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

ColumnLayout {
    // Flickable

    id: root

    // Capture the context property so child components can access it via root.settingsBridge
    readonly property var settingsBridge: appSettings
    // Inline constants matching monolith's Constants.qml
    readonly property int layoutListMinHeight: Kirigami.Units.gridUnit * 20
    // View mode: 0 = Snapping Layouts, 1 = Auto Tile Algorithms
    property int viewMode: 0

    // m-15: Extract URL-to-path helper to avoid duplicating regex in FileDialogs
    function filePathFromUrl(url) {
        return url.toString().replace(/^file:\/\/+/, "/");
    }

    spacing: 0

    // Reset to Snapping Layouts when autotiling is disabled
    Connections {
        function onAutotileEnabledChanged() {
            if (!root.settingsBridge.autotileEnabled && root.viewMode !== 0) {
                root.viewMode = 0;
                layoutGrid.selectedLayoutId = "";
                layoutGrid.rebuildModel();
                layoutGrid.selectDefaultLayout(0);
            }
        }

        target: root.settingsBridge
    }

    // ─── Sticky Toolbar ──────────────────────────────────────────────────
    LayoutToolbar {
        Layout.fillWidth: true
        Layout.bottomMargin: Kirigami.Units.smallSpacing
        appSettings: root.settingsBridge
        viewMode: root.viewMode
        onRequestCreateNewLayout: newLayoutDialog.open()
        onRequestCreateNewAlgorithm: newAlgorithmDialog.open()
        onRequestImportLayout: importDialog.open()
        onRequestImportFromKZones: settingsController.importFromKZones()
        onRequestImportKZonesFile: kzonesFileDialog.open()
        onRequestOpenLayoutsFolder: settingsController.openLayoutsFolder()
        onRequestImportAlgorithm: algorithmImportDialog.open()
        onRequestOpenAlgorithmsFolder: settingsController.openAlgorithmsFolder()
        onViewModeRequested: (mode) => {
            root.viewMode = mode;
            layoutGrid.selectedLayoutId = "";
            // rebuildModel() already triggered by filterBar.onViewModeChanged → loadState → filterSettingsChanged
            layoutGrid.selectDefaultLayout(mode);
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        Layout.bottomMargin: Kirigami.Units.smallSpacing
    }

    // ─── Filter / Group / Sort bar ──────────────────────────────────────
    LayoutFilterBar {
        id: filterBar

        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        Layout.bottomMargin: Kirigami.Units.smallSpacing
        viewMode: root.viewMode
        onFilterSettingsChanged: layoutGrid.rebuildModel()
    }

    Kirigami.Separator {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        Layout.bottomMargin: Kirigami.Units.smallSpacing
    }

    // ─── Scrollable content ──────────────────────────────────────────────
    Flickable {
        Layout.fillWidth: true
        Layout.fillHeight: true
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: content

            width: parent.width
            spacing: Kirigami.Units.largeSpacing

            // ─── Layout Grid (grouped by aspect ratio) ─────────────────────
            ListView {
                id: layoutGrid

                // Responsive cell sizing for Flow delegates
                readonly property real minCellWidth: Kirigami.Units.gridUnit * 14
                readonly property int columnCount: Math.max(2, Math.floor(width / minCellWidth))
                readonly property real cellWidth: width / columnCount
                readonly property real cellHeight: Kirigami.Units.gridUnit * 12
                // Selected layout tracking (across sections)
                property string selectedLayoutId: ""
                // Capability group definitions for tiling view — hoisted to avoid
                // recreating on every rebuildModel() call.
                // NOTE: items matching multiple capabilities appear in multiple groups;
                // the same card will be highlighted in both sections simultaneously.
                readonly property var tilingCapabilityGroups: [{
                    "key": "masterCount",
                    "label": i18n("Master Count"),
                    "order": 0,
                    "test": (a) => {
                        return a.supportsMasterCount === true;
                    }
                }, {
                    "key": "splitRatio",
                    "label": i18n("Split Ratio"),
                    "order": 1,
                    "test": (a) => {
                        return a.supportsSplitRatio === true;
                    }
                }, {
                    "key": "overlapping",
                    "label": i18n("Overlapping Zones"),
                    "order": 2,
                    "test": (a) => {
                        return a.producesOverlappingZones === true;
                    }
                }, {
                    "key": "persistent",
                    "label": i18n("Persistent (Memory)"),
                    "order": 3,
                    "test": (a) => {
                        return a.memory === true;
                    }
                }]

                function rebuildModel() {
                    let allLayouts = settingsController.layouts;
                    let filtered = [];
                    for (let i = 0; i < allLayouts.length; i++) {
                        let isAutotile = allLayouts[i].isAutotile === true;
                        if (root.viewMode === 0 && !isAutotile)
                            filtered.push(allLayouts[i]);
                        else if (root.viewMode === 1 && isAutotile)
                            filtered.push(allLayouts[i]);
                    }
                    let search = filterBar.filterText.toLowerCase();
                    if (root.viewMode === 0)
                        filtered = Logic.applySnappingFilters(filtered, search, filterBar);
                    else
                        filtered = Logic.applyTilingFilters(filtered, search, filterBar);
                    let groups = buildGroups(filtered, filterBar.groupByIndex);
                    Logic.sortItems(groups, filterBar.sortByIndex, filterBar.sortAscending);
                    model = Logic.finalizeGroups(groups);
                }

                function selectDefaultLayout(mode) {
                    let defaultId = (mode === 1) ? ("autotile:" + root.settingsBridge.defaultAutotileAlgorithm) : root.settingsBridge.defaultLayoutId;
                    if (defaultId)
                        selectedLayoutId = defaultId;

                }

                function selectLayoutById(layoutId) {
                    if (layoutId)
                        selectedLayoutId = layoutId;

                    return layoutId !== "";
                }

                // Grouping — kept in QML because group labels require i18n/i18np
                function buildGroups(filtered, groupIdx) {
                    if (root.viewMode === 1) {
                        if (groupIdx === filterBar.groupCapability)
                            return Logic.groupByCapability(filtered, tilingCapabilityGroups, i18n("Other"));
                        else if (groupIdx === filterBar.groupTilingSource)
                            return Logic.groupByBoolKey(filtered, (item) => {
                            return Logic.isBuiltIn(item);
                        }, "builtin", i18n("Built-in"), "user", i18n("User Scripts"));
                        else if (groupIdx === filterBar.groupPersistent)
                            return Logic.groupByBoolKey(filtered, (item) => {
                            return item.memory === true;
                        }, "persistent", i18n("Persistent"), "stateless", i18n("Stateless"));
                        return Logic.ungrouped(filtered);
                    }
                    // Snapping grouping
                    if (groupIdx === filterBar.groupAspectRatio)
                        return Logic.groupByAspectRatio(filtered);
                    else if (groupIdx === filterBar.groupZoneCount)
                        return Logic.groupByZoneCount(filtered, (count) => {
                        return i18np("%1 zone", "%1 zones", count);
                    }, i18n("Unknown"));
                    else if (groupIdx === filterBar.groupAutoManual)
                        return Logic.groupByBoolKey(filtered, (item) => {
                        return item.autoAssign === true;
                    }, "auto", i18n("Auto"), "manual", i18n("Manual"));
                    else if (groupIdx === filterBar.groupSource)
                        return Logic.groupByBoolKey(filtered, (item) => {
                        return Logic.isBuiltIn(item);
                    }, "builtin", i18n("Built-in"), "user", i18n("User Layouts"));
                    return Logic.ungrouped(filtered);
                }

                Layout.topMargin: Kirigami.Units.largeSpacing
                Accessible.name: i18n("Layout grid")
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                Layout.rightMargin: Kirigami.Units.smallSpacing
                Layout.preferredHeight: Math.max(root.layoutListMinHeight, contentHeight)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                spacing: Kirigami.Units.largeSpacing
                model: []
                Component.onCompleted: {
                    rebuildModel();
                    selectDefaultLayout(root.viewMode);
                }

                Connections {
                    function onLayoutsChanged() {
                        layoutGrid.rebuildModel();
                    }

                    function onLayoutAdded(layoutId) {
                        Qt.callLater(() => {
                            layoutGrid.selectLayoutById(layoutId);
                        });
                    }

                    target: settingsController
                }

                // Empty state
                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 4
                    visible: layoutGrid.count === 0
                    text: {
                        if (filterBar.hasActiveFilters)
                            return root.viewMode === 1 ? i18n("No matching algorithms") : i18n("No matching layouts");

                        return root.viewMode === 1 ? i18n("No autotile algorithms available") : i18n("No layouts available");
                    }
                    explanation: {
                        if (filterBar.hasActiveFilters) {
                            let hints = [];
                            if (root.viewMode === 0 && !filterBar.showBuiltInLayouts && !filterBar.showUserLayouts)
                                hints.push(i18n("Both Built-in and User layout sources are hidden"));

                            if (root.viewMode === 0 && !filterBar.showAutoLayouts && !filterBar.showManualLayouts)
                                hints.push(i18n("Both Auto and Manual layout types are hidden"));

                            if (root.viewMode === 1 && !filterBar.showBuiltInAlgorithms && !filterBar.showUserAlgorithms)
                                hints.push(i18n("Both Built-in and User algorithm sources are hidden"));

                            return hints.length > 0 ? hints.join("\n") : i18n("Try adjusting your filters or search terms");
                        }
                        return root.viewMode === 1 ? i18n("Enable autotiling to use tiling algorithms") : i18n("Start the PlasmaZones daemon or create a new layout");
                    }

                    helpfulAction: Kirigami.Action {
                        visible: filterBar.hasActiveFilters
                        text: i18n("Reset Filters")
                        icon.name: "edit-reset"
                        onTriggered: filterBar.resetFilters()
                    }

                }

                // ─── Section Delegate (header + Flow of layout cards) ────────
                delegate: ColumnLayout {
                    id: sectionDelegate

                    required property var modelData
                    required property int index

                    width: layoutGrid.width
                    spacing: Kirigami.Units.smallSpacing

                    // Section header
                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.smallSpacing
                        text: sectionDelegate.modelData.label || ""
                        visible: text.length > 0
                        font.weight: Font.DemiBold
                        opacity: 0.6
                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.smallSpacing
                        Layout.rightMargin: Kirigami.Units.smallSpacing
                        visible: (sectionDelegate.modelData.label || "").length > 0
                    }

                    // Flow grid of layout cards
                    Flow {
                        Layout.fillWidth: true
                        spacing: 0

                        Repeater {
                            model: sectionDelegate.modelData.layouts || []

                            LayoutGridDelegate {
                                appSettings: root.settingsBridge
                                cellWidth: layoutGrid.cellWidth
                                cellHeight: layoutGrid.cellHeight
                                viewMode: root.viewMode
                                isSelected: String(modelData.id) === layoutGrid.selectedLayoutId
                                onSelected: (idx) => {
                                    layoutGrid.selectedLayoutId = String(modelData.id);
                                }
                                onActivated: (layoutId) => {
                                    settingsController.editLayout(layoutId);
                                }
                                onDeleteRequested: (layout) => {
                                    deleteConfirmDialog.layoutToDelete = layout;
                                    deleteConfirmDialog.open();
                                }
                                onContextMenuRequested: (layout) => {
                                    window.showLayoutContextMenu(layout);
                                }
                            }

                        }

                    }

                }

            }

        }

    }

    // Import file dialog
    FileDialog {
        id: importDialog

        title: i18n("Import Layout")
        nameFilters: [i18n("JSON files") + " (*.json)", i18n("All files") + " (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            settingsController.importLayout(root.filePathFromUrl(selectedFile));
        }
    }

    // Export file dialog
    FileDialog {
        id: exportDialog

        property string layoutId: ""

        title: i18n("Export Layout")
        nameFilters: [i18n("JSON files") + " (*.json)"]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            settingsController.exportLayout(exportDialog.layoutId, root.filePathFromUrl(selectedFile));
        }
    }

    // Algorithm import dialog
    FileDialog {
        id: algorithmImportDialog

        title: i18n("Import Tiling Algorithm")
        nameFilters: [i18n("JavaScript files") + " (*.js)", i18n("All files") + " (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (settingsController.importAlgorithm(root.filePathFromUrl(selectedFile))) {
                if (window && window.showToast)
                    window.showToast(i18n("Algorithm imported"));

            }
        }
    }

    // KZones file import dialog
    FileDialog {
        id: kzonesFileDialog

        title: i18n("Import KZones Layout File")
        nameFilters: [i18n("JSON files") + " (*.json)", i18n("All files") + " (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            settingsController.importFromKZonesFile(root.filePathFromUrl(selectedFile));
        }
    }

    // KZones import result notification — uses Main.qml's toast
    Connections {
        function onKzonesImportFinished(count, message) {
            if (window && window.showToast)
                window.showToast(message);

        }

        target: settingsController
    }

    // Connect context menu signals from Main.qml to local dialogs
    Connections {
        function onDeleteRequested(layout) {
            deleteConfirmDialog.layoutToDelete = layout;
            deleteConfirmDialog.open();
        }

        function onExportRequested(layoutId) {
            if (layoutId.startsWith("autotile:")) {
                algorithmExportDialog.algorithmId = settingsController.algorithmIdFromLayoutId(layoutId);
                algorithmExportDialog.open();
            } else {
                exportDialog.layoutId = layoutId;
                exportDialog.open();
            }
        }

        target: window.layoutContextMenu
    }

    // New Layout wizard dialog
    NewLayoutDialog {
        id: newLayoutDialog

        appSettings: root.settingsBridge
        controller: settingsController
    }

    // New Algorithm wizard dialog
    NewAlgorithmDialog {
        id: newAlgorithmDialog

        controller: settingsController
    }

    // Algorithm created/failed signals from C++ (fires after AlgorithmRegistry picks up the new file)
    Connections {
        function onAlgorithmCreated(algorithmId) {
            // Always rebuild so the new algorithm is available; only switch view
            // and auto-select if the user is already looking at the tiling view
            // (avoids jarring view switch when duplicating from a different context)
            layoutGrid.rebuildModel();
            if (root.viewMode === 1)
                layoutGrid.selectedLayoutId = "autotile:" + algorithmId;

        }

        function onAlgorithmOperationFailed(reason) {
            // Only show toast when the wizard dialog is closed — if the dialog
            // is open, it shows the error inline via its own Connections block
            if (!newAlgorithmDialog.opened && window && window.showToast)
                window.showToast(reason);

        }

        function onLayoutOperationFailed(reason) {
            // Only show toast when the wizard dialog is closed — if the dialog
            // is open, it shows the error inline via its own Connections block
            if (!newLayoutDialog.opened && window && window.showToast)
                window.showToast(reason);

        }

        target: settingsController
    }

    // Algorithm export file dialog
    FileDialog {
        id: algorithmExportDialog

        property string algorithmId: ""

        title: i18n("Export Algorithm")
        nameFilters: [i18n("JavaScript files") + " (*.js)"]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            settingsController.exportAlgorithm(algorithmExportDialog.algorithmId, root.filePathFromUrl(selectedFile));
        }
    }

    // Delete confirmation dialog (handles both layouts and algorithms)
    Kirigami.PromptDialog {
        id: deleteConfirmDialog

        property var layoutToDelete: null
        readonly property bool isAlgorithm: layoutToDelete && layoutToDelete.isAutotile === true

        title: isAlgorithm ? i18n("Delete Algorithm") : i18n("Delete Layout")
        subtitle: layoutToDelete ? i18n("Are you sure you want to delete \"%1\"?", layoutToDelete.name || "") : ""
        standardButtons: Kirigami.Dialog.NoButton
        onRejected: layoutToDelete = null
        onClosed: layoutToDelete = null
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    if (deleteConfirmDialog.layoutToDelete) {
                        if (deleteConfirmDialog.isAlgorithm) {
                            let algoId = settingsController.algorithmIdFromLayoutId(deleteConfirmDialog.layoutToDelete.id);
                            settingsController.deleteAlgorithm(algoId);
                        } else {
                            settingsController.deleteLayout(deleteConfirmDialog.layoutToDelete.id);
                        }
                        deleteConfirmDialog.layoutToDelete = null;
                    }
                    deleteConfirmDialog.close();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: deleteConfirmDialog.close()
            }
        ]
    }

}
