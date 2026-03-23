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
    // Shared aspect ratio labels (used in context menu + section headers)
    readonly property var aspectRatioLabels: ({
        "any": i18n("All Monitors"),
        "standard": i18n("Standard (16:9)"),
        "ultrawide": i18n("Ultrawide (21:9)"),
        "super-ultrawide": i18n("Super-Ultrawide (32:9)"),
        "portrait": i18n("Portrait (9:16)")
    })

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
        onRequestCreateNewLayout: settingsController.createNewLayout()
        onRequestImportLayout: importDialog.open()
        onRequestImportFromKZones: settingsController.importFromKZones()
        onRequestImportKZonesFile: kzonesFileDialog.open()
        onRequestOpenLayoutsFolder: settingsController.openLayoutsFolder()
        onViewModeRequested: (mode) => {
            root.viewMode = mode;
            layoutGrid.selectedLayoutId = "";
            layoutGrid.rebuildModel();
            layoutGrid.selectDefaultLayout(mode);
        }
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

            // ─── Context Menu ──────────────────────────────────────────────────
            // Shared context menu -- single instance, avoids Qt6 per-delegate crash.
            // Screen items are flat (no nested Menu) to avoid the Qt6 submenu crash.
            Menu {
                id: layoutContextMenu

                property var layout: null
                property var _screenItems: []
                readonly property bool isAutotile: layout && layout.isAutotile === true
                readonly property string layoutId: layout ? (layout.id || "") : ""

                function showForLayout(layout) {
                    layoutContextMenu.layout = layout;
                    // Remove previously created screen items
                    for (let j = 0; j < _screenItems.length; j++) {
                        layoutContextMenu.removeItem(_screenItems[j]);
                        _screenItems[j].destroy();
                    }
                    _screenItems = [];
                    // Insert flat "Edit on <screen>" items when multi-monitor
                    if (settingsController.screens && settingsController.screens.length > 1) {
                        let screens = settingsController.screens;
                        // Find the index of postScreenSeparator so we can insert before it
                        let insertIdx = -1;
                        for (let k = 0; k < layoutContextMenu.count; k++) {
                            if (layoutContextMenu.itemAt(k) === postScreenSeparator) {
                                insertIdx = k;
                                break;
                            }
                        }
                        for (let i = 0; i < screens.length; i++) {
                            let s = screens[i];
                            let parts = [s.manufacturer || "", s.model || ""].filter((p) => {
                                return p !== "";
                            });
                            let label = parts.length > 0 ? parts.join(" ") : (s.name || "");
                            if (s.resolution)
                                label += " (" + s.resolution + ")";

                            let item = screenMenuItemComponent.createObject(layoutContextMenu, {
                                "text": i18n("Edit on %1", label),
                                "icon.name": s.isPrimary ? "starred-symbolic" : "monitor"
                            });
                            let lid = layout.id;
                            item.triggered.connect(function() {
                                settingsController.editLayout(lid);
                            });
                            if (insertIdx >= 0)
                                layoutContextMenu.insertItem(insertIdx + i, item);
                            else
                                layoutContextMenu.addItem(item);
                            _screenItems.push(item);
                        }
                    }
                    // Hide screen separators when no screen items
                    screenSeparator.visible = _screenItems.length > 0;
                    postScreenSeparator.visible = _screenItems.length > 0;
                    layoutContextMenu.popup();
                }

                // -- Edit --
                MenuItem {
                    text: i18n("Edit")
                    icon.name: "document-edit"
                    onTriggered: settingsController.editLayout(layoutContextMenu.layoutId)
                }

                // Dynamic "Edit on <screen>" items are inserted here by showForLayout()
                MenuSeparator {
                    id: screenSeparator

                    visible: false
                }

                MenuSeparator {
                    id: postScreenSeparator

                    visible: false
                }

                // -- State --
                MenuItem {
                    text: i18n("Set as Default")
                    icon.name: "favorite"
                    enabled: {
                        if (!layoutContextMenu.layout)
                            return false;

                        if (root.viewMode === 1)
                            return layoutContextMenu.layoutId !== ("autotile:" + root.settingsBridge.autotileAlgorithm);

                        return layoutContextMenu.layoutId !== root.settingsBridge.defaultLayoutId;
                    }
                    onTriggered: {
                        if (root.viewMode === 1)
                            root.settingsBridge.autotileAlgorithm = layoutContextMenu.layoutId.replace("autotile:", "");
                        else
                            root.settingsBridge.defaultLayoutId = layoutContextMenu.layoutId;
                    }
                }

                MenuItem {
                    text: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? i18n("Show in Zone Selector") : i18n("Hide from Zone Selector")
                    icon.name: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? "view-visible" : "view-hidden"
                    onTriggered: settingsController.setLayoutHidden(layoutContextMenu.layoutId, !(layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector))
                }

                MenuItem {
                    text: layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true ? i18n("Disable Auto-assign") : i18n("Enable Auto-assign")
                    icon.name: layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true ? "window-duplicate" : "window-new"
                    visible: !layoutContextMenu.isAutotile
                    onTriggered: settingsController.setLayoutAutoAssign(layoutContextMenu.layoutId, !(layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true))
                }

                Menu {
                    title: i18n("Aspect Ratio")
                    icon.name: "transform-crop"
                    visible: !layoutContextMenu.isAutotile

                    MenuItem {
                        text: root.aspectRatioLabels["any"]
                        checkable: true
                        checked: layoutContextMenu.layout && (layoutContextMenu.layout.aspectRatioClass || "any") === "any"
                        onTriggered: settingsController.setLayoutAspectRatio(layoutContextMenu.layoutId, 0)
                    }

                    MenuItem {
                        text: root.aspectRatioLabels["standard"]
                        checkable: true
                        checked: layoutContextMenu.layout && layoutContextMenu.layout.aspectRatioClass === "standard"
                        onTriggered: settingsController.setLayoutAspectRatio(layoutContextMenu.layoutId, 1)
                    }

                    MenuItem {
                        text: root.aspectRatioLabels["ultrawide"]
                        checkable: true
                        checked: layoutContextMenu.layout && layoutContextMenu.layout.aspectRatioClass === "ultrawide"
                        onTriggered: settingsController.setLayoutAspectRatio(layoutContextMenu.layoutId, 2)
                    }

                    MenuItem {
                        text: root.aspectRatioLabels["super-ultrawide"]
                        checkable: true
                        checked: layoutContextMenu.layout && layoutContextMenu.layout.aspectRatioClass === "super-ultrawide"
                        onTriggered: settingsController.setLayoutAspectRatio(layoutContextMenu.layoutId, 3)
                    }

                    MenuItem {
                        text: root.aspectRatioLabels["portrait"]
                        checkable: true
                        checked: layoutContextMenu.layout && layoutContextMenu.layout.aspectRatioClass === "portrait"
                        onTriggered: settingsController.setLayoutAspectRatio(layoutContextMenu.layoutId, 4)
                    }

                }

                // -- Manage --
                MenuSeparator {
                    visible: root.viewMode === 0 && !layoutContextMenu.isAutotile
                }

                MenuItem {
                    text: i18n("Duplicate")
                    icon.name: "edit-copy"
                    visible: root.viewMode === 0 && !layoutContextMenu.isAutotile
                    onTriggered: settingsController.duplicateLayout(layoutContextMenu.layoutId)
                }

                MenuItem {
                    text: i18n("Export")
                    icon.name: "document-export"
                    visible: root.viewMode === 0
                    onTriggered: {
                        exportDialog.layoutId = layoutContextMenu.layoutId;
                        exportDialog.open();
                    }
                }

                MenuSeparator {
                    visible: root.viewMode === 0 && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem && !layoutContextMenu.isAutotile
                }

                MenuItem {
                    text: i18n("Delete")
                    icon.name: "edit-delete"
                    visible: root.viewMode === 0 && layoutContextMenu.layout && !layoutContextMenu.layout.isSystem && !layoutContextMenu.isAutotile
                    onTriggered: {
                        deleteConfirmDialog.layoutToDelete = layoutContextMenu.layout;
                        deleteConfirmDialog.open();
                    }
                }

                Component {
                    id: screenMenuItemComponent

                    MenuItem {
                    }

                }

            }

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

                function rebuildModel() {
                    let allLayouts = settingsController.layouts;
                    // Filter by view mode
                    let filtered = [];
                    for (let i = 0; i < allLayouts.length; i++) {
                        let isAutotile = allLayouts[i].isAutotile === true;
                        if (root.viewMode === 0 && !isAutotile)
                            filtered.push(allLayouts[i]);
                        else if (root.viewMode === 1 && isAutotile)
                            filtered.push(allLayouts[i]);
                    }
                    // Group by aspect ratio class
                    let aspectOrder = ["any", "standard", "ultrawide", "super-ultrawide", "portrait"];
                    let aspectLabels = root.aspectRatioLabels;
                    let groups = {
                    };
                    for (let i = 0; i < filtered.length; i++) {
                        let cls = filtered[i].aspectRatioClass || "any";
                        if (!groups[cls])
                            groups[cls] = [];

                        groups[cls].push(filtered[i]);
                    }
                    // Sort each group alphabetically
                    for (let cls in groups) {
                        groups[cls].sort((a, b) => {
                            return (a.name || "").localeCompare(b.name || "");
                        });
                    }
                    // Build section model: [{label, layouts}, ...]
                    let sections = [];
                    let groupCount = 0;
                    for (let a = 0; a < aspectOrder.length; a++) {
                        if (groups[aspectOrder[a]] && groups[aspectOrder[a]].length > 0)
                            groupCount++;

                    }
                    for (let a = 0; a < aspectOrder.length; a++) {
                        let cls = aspectOrder[a];
                        if (groups[cls] && groups[cls].length > 0)
                            sections.push({
                            "label": groupCount > 1 ? aspectLabels[cls] : "",
                            "layouts": groups[cls]
                        });

                    }
                    model = sections;
                }

                function selectDefaultLayout(mode) {
                    let defaultId = (mode === 1) ? ("autotile:" + root.settingsBridge.autotileAlgorithm) : root.settingsBridge.defaultLayoutId;
                    if (defaultId)
                        selectedLayoutId = defaultId;

                }

                function selectLayoutById(layoutId) {
                    if (layoutId)
                        selectedLayoutId = layoutId;

                    return layoutId !== "";
                }

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
                    text: root.viewMode === 1 ? i18n("No autotile algorithms available") : i18n("No layouts available")
                    explanation: root.viewMode === 1 ? i18n("Enable autotiling to use tiling algorithms") : i18n("Start the PlasmaZones daemon or create a new layout")
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
                                    layoutContextMenu.showForLayout(layout);
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
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            settingsController.importLayout(selectedFile.toString().replace(/^file:\/\/+/, "/"));
        }
    }

    // Export file dialog
    FileDialog {
        id: exportDialog

        property string layoutId: ""

        title: i18n("Export Layout")
        nameFilters: ["JSON files (*.json)"]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            settingsController.exportLayout(exportDialog.layoutId, selectedFile.toString().replace(/^file:\/\/+/, "/"));
        }
    }

    // KZones file import dialog
    FileDialog {
        id: kzonesFileDialog

        title: i18n("Import KZones Layout File")
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            settingsController.importFromKZonesFile(selectedFile.toString().replace(/^file:\/\/+/, "/"));
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

    // Delete confirmation dialog
    Kirigami.PromptDialog {
        id: deleteConfirmDialog

        property var layoutToDelete: null

        title: i18n("Delete Layout")
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
                        settingsController.deleteLayout(deleteConfirmDialog.layoutToDelete.id);
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
