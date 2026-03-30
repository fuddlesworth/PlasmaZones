// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

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
            layoutGrid.rebuildModel();
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
                // Source (built-in vs user scripts)
                // Persistent (stateful vs stateless)
                // None
                // Auto / Manual
                // Source (built-in vs user-created)
                // Note: snapping layouts use hasSystemOrigin; algorithms do not
                // None

                id: layoutGrid

                // Responsive cell sizing for Flow delegates
                readonly property real minCellWidth: Kirigami.Units.gridUnit * 14
                readonly property int columnCount: Math.max(2, Math.floor(width / minCellWidth))
                readonly property real cellWidth: width / columnCount
                readonly property real cellHeight: Kirigami.Units.gridUnit * 12
                // Selected layout tracking (across sections)
                property string selectedLayoutId: ""

                function rebuildModel() {
                    let allLayouts = settingsController.layouts;
                    // ── Step 1: filter by view mode ──────────────────────────
                    let filtered = [];
                    for (let i = 0; i < allLayouts.length; i++) {
                        let isAutotile = allLayouts[i].isAutotile === true;
                        if (root.viewMode === 0 && !isAutotile)
                            filtered.push(allLayouts[i]);
                        else if (root.viewMode === 1 && isAutotile)
                            filtered.push(allLayouts[i]);
                    }
                    // ── Step 2: apply filters ────────────────────────────────
                    filtered = applyFilters(filtered);
                    // ── Step 3: group ────────────────────────────────────────
                    let groups = buildGroups(filtered, filterBar.groupByIndex);
                    // ── Step 4: sort items within each group ─────────────────
                    let ascending = filterBar.sortAscending;
                    let sortIdx = filterBar.sortByIndex;
                    for (let key in groups) {
                        groups[key].items.sort((a, b) => {
                            let cmp;
                            if (sortIdx === 1) {
                                cmp = (a.zoneCount || 0) - (b.zoneCount || 0);
                                if (cmp === 0)
                                    cmp = (a.name || "").localeCompare(b.name || "");

                            } else {
                                cmp = (a.name || "").localeCompare(b.name || "");
                            }
                            return ascending ? cmp : -cmp;
                        });
                    }
                    // ── Step 5: sort groups and build model ──────────────────
                    let sorted = Object.values(groups).sort((a, b) => {
                        return a.order - b.order;
                    });
                    let nonEmpty = sorted.filter((g) => {
                        return g.items.length > 0;
                    });
                    model = nonEmpty.map((g) => {
                        return ({
                            "label": nonEmpty.length > 1 ? g.label : "",
                            "layouts": g.items
                        });
                    });
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

                function applyFilters(filtered) {
                    // Text search
                    let search = filterBar.filterText.toLowerCase();
                    if (search.length > 0)
                        filtered = filtered.filter((item) => {
                        return (item.name || "").toLowerCase().includes(search) || (item.description || "").toLowerCase().includes(search);
                    });

                    // Hidden filter (applies to both modes)
                    if (!filterBar.showHidden)
                        filtered = filtered.filter((item) => {
                        return item.hiddenFromSelector !== true;
                    });

                    if (root.viewMode === 0) {
                        // Snapping: aspect-ratio class filter
                        let arMap = {
                            "any": filterBar.showAspectAny,
                            "standard": filterBar.showAspectStandard,
                            "ultrawide": filterBar.showAspectUltrawide,
                            "super-ultrawide": filterBar.showAspectSuperUltrawide,
                            "portrait": filterBar.showAspectPortrait
                        };
                        filtered = filtered.filter((item) => {
                            let cls = item.aspectRatioClass || "any";
                            // Unknown classes pass through (arMap[cls] is undefined, not false)
                            return arMap[cls] !== false;
                        });
                        // Source filter (built-in vs user)
                        filtered = filtered.filter((item) => {
                            if ((item.isSystem || item.hasSystemOrigin) && !filterBar.showBuiltInLayouts)
                                return false;

                            if (!item.isSystem && !item.hasSystemOrigin && !filterBar.showUserLayouts)
                                return false;

                            return true;
                        });
                        // Auto / Manual filter
                        filtered = filtered.filter((item) => {
                            if (item.autoAssign === true && !filterBar.showAutoLayouts)
                                return false;

                            if (item.autoAssign !== true && !filterBar.showManualLayouts)
                                return false;

                            return true;
                        });
                    } else {
                        // Tiling: source filter (algorithms don't have hasSystemOrigin)
                        filtered = filtered.filter((item) => {
                            if (item.isSystem && !filterBar.showBuiltInAlgorithms)
                                return false;

                            if (!item.isSystem && !filterBar.showUserAlgorithms)
                                return false;

                            return true;
                        });
                        // Capability filters (hide when unchecked)
                        filtered = filtered.filter((item) => {
                            if (!filterBar.showMasterCount && item.supportsMasterCount === true)
                                return false;

                            if (!filterBar.showSplitRatio && item.supportsSplitRatio === true)
                                return false;

                            if (!filterBar.showOverlapping && item.producesOverlappingZones === true)
                                return false;

                            if (!filterBar.showPersistent && item.memory === true)
                                return false;

                            return true;
                        });
                    }
                    return filtered;
                }

                function buildGroups(filtered, groupIdx) {
                    let groups = {
                    };
                    if (root.viewMode === 1) {
                        // ── Tiling grouping ──────────────────────────────────
                        if (groupIdx === 0) {
                            // Capability (algorithms can appear in multiple groups)
                            let capGroups = [{
                                "key": "masterCount",
                                "label": i18n("Master Count"),
                                "order": 0,
                                "test": (a) => {
                                    return a.supportsMasterCount === true;
                                }
                            }, {
                                "key": "overlapping",
                                "label": i18n("Overlapping Zones"),
                                "order": 1,
                                "test": (a) => {
                                    return a.producesOverlappingZones === true;
                                }
                            }, {
                                "key": "splitRatio",
                                "label": i18n("Split Ratio"),
                                "order": 2,
                                "test": (a) => {
                                    return a.supportsSplitRatio === true;
                                }
                            }];
                            for (let g = 0; g < capGroups.length; g++) {
                                let cap = capGroups[g];
                                groups[cap.key] = {
                                    "items": [],
                                    "order": cap.order,
                                    "label": cap.label
                                };
                            }
                            for (let i = 0; i < filtered.length; i++) {
                                let placed = false;
                                for (let g = 0; g < capGroups.length; g++) {
                                    if (capGroups[g].test(filtered[i])) {
                                        groups[capGroups[g].key].items.push(filtered[i]);
                                        placed = true;
                                    }
                                }
                                if (!placed) {
                                    if (!groups["other"])
                                        groups["other"] = {
                                        "items": [],
                                        "order": 99,
                                        "label": i18n("Other")
                                    };

                                    groups["other"].items.push(filtered[i]);
                                }
                            }
                        } else if (groupIdx === 1)
                            groups = groupByBoolKey(filtered, (item) => {
                            return item.isSystem;
                        }, "builtin", i18n("Built-in"), "user", i18n("User Scripts"));
                        else if (groupIdx === 2)
                            groups = groupByBoolKey(filtered, (item) => {
                            return item.memory === true;
                        }, "persistent", i18n("Persistent"), "stateless", i18n("Stateless"));
                        else
                            groups["all"] = {
                            "items": filtered,
                            "order": 0,
                            "label": ""
                        };
                    } else {
                        // ── Snapping grouping ────────────────────────────────
                        if (groupIdx === 0) {
                            // Aspect ratio (data-driven from C++)
                            for (let i = 0; i < filtered.length; i++) {
                                let key = filtered[i].sectionKey || "default";
                                if (!groups[key])
                                    groups[key] = {
                                    "items": [],
                                    "order": filtered[i].sectionOrder !== undefined ? filtered[i].sectionOrder : 0,
                                    "label": filtered[i].sectionLabel || ""
                                };

                                groups[key].items.push(filtered[i]);
                            }
                        } else if (groupIdx === 1) {
                            // Zone count (0 or undefined → "Unknown" group at the end)
                            for (let i = 0; i < filtered.length; i++) {
                                let count = filtered[i].zoneCount || 0;
                                let key = count > 0 ? "zones-" + count : "zones-unknown";
                                if (!groups[key])
                                    groups[key] = {
                                    "items": [],
                                    "order": count > 0 ? count : 9999,
                                    "label": count > 0 ? i18np("%1 zone", "%1 zones", count) : i18n("Unknown")
                                };

                                groups[key].items.push(filtered[i]);
                            }
                        } else if (groupIdx === 2)
                            groups = groupByBoolKey(filtered, (item) => {
                            return item.autoAssign === true;
                        }, "auto", i18n("Auto"), "manual", i18n("Manual"));
                        else if (groupIdx === 3)
                            groups = groupByBoolKey(filtered, (item) => {
                            return item.isSystem || item.hasSystemOrigin;
                        }, "builtin", i18n("Built-in"), "user", i18n("User Layouts"));
                        else
                            groups["all"] = {
                            "items": filtered,
                            "order": 0,
                            "label": ""
                        };
                    }
                    return groups;
                }

                function groupByBoolKey(items, testFn, trueKey, trueLabel, falseKey, falseLabel) {
                    let groups = {
                    };
                    for (let i = 0; i < items.length; i++) {
                        let item = items[i];
                        let match = testFn(item);
                        let key = match ? trueKey : falseKey;
                        if (!groups[key])
                            groups[key] = {
                            "items": [],
                            "order": match ? 0 : 1,
                            "label": match ? trueLabel : falseLabel
                        };

                        groups[key].items.push(item);
                    }
                    return groups;
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
                            if (root.viewMode === 0 && !filterBar.showBuiltInLayouts && !filterBar.showUserLayouts)
                                return i18n("Both Built-in and User layout sources are hidden");

                            if (root.viewMode === 0 && !filterBar.showAutoLayouts && !filterBar.showManualLayouts)
                                return i18n("Both Auto and Manual layout types are hidden");

                            if (root.viewMode === 1 && !filterBar.showBuiltInAlgorithms && !filterBar.showUserAlgorithms)
                                return i18n("Both Built-in and User algorithm sources are hidden");

                            return i18n("Try adjusting your filters or search terms");
                        }
                        return root.viewMode === 1 ? i18n("Enable autotiling to use tiling algorithms") : i18n("Start the PlasmaZones daemon or create a new layout");
                    }

                    helpfulAction: Kirigami.Action {
                        visible: filterBar.hasActiveFilters
                        text: i18n("Reset Filters")
                        icon.name: "edit-reset"
                        onTriggered: {
                            filterBar.resetFilters();
                            layoutGrid.rebuildModel();
                        }
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
        nameFilters: [i18n("JSON files (*.json)"), i18n("All files (*)")]
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
        nameFilters: [i18n("JSON files (*.json)")]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            settingsController.exportLayout(exportDialog.layoutId, root.filePathFromUrl(selectedFile));
        }
    }

    // Algorithm import dialog
    FileDialog {
        id: algorithmImportDialog

        title: i18n("Import Tiling Algorithm")
        nameFilters: [i18n("JavaScript files (*.js)"), i18n("All files (*)")]
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
        nameFilters: [i18n("JSON files (*.json)"), i18n("All files (*)")]
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
        nameFilters: ["JavaScript files (*.js)"]
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
