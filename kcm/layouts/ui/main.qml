// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: root

    // Capture the context property so child components can access it via root.kcmModule
    readonly property var kcmModule: kcm
    // Inline constants matching monolith's Constants.qml
    readonly property int layoutListMinHeight: Kirigami.Units.gridUnit * 20
    // View mode: 0 = Snapping Layouts, 1 = Auto Tile Algorithms
    property int viewMode: 0
    // Current layout helper for toolbar
    readonly property var currentLayout: layoutGrid.currentItem ? layoutGrid.currentItem.modelData : null

    topPadding: Kirigami.Units.largeSpacing
    bottomPadding: Kirigami.Units.largeSpacing
    leftPadding: Kirigami.Units.largeSpacing
    rightPadding: Kirigami.Units.largeSpacing

    // Reset to Snapping Layouts when autotiling is disabled
    Connections {
        function onAutotileEnabledChanged() {
            if (!root.kcmModule.autotileEnabled && root.viewMode !== 0) {
                root.viewMode = 0;
                layoutGrid.currentIndex = -1;
                layoutGrid.rebuildModel();
                layoutGrid.selectDefaultLayout(0);
            }
        }

        target: root.kcmModule
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.largeSpacing

        // Toolbar
        LayoutToolbar {
            Layout.fillWidth: true
            kcm: root.kcmModule
            currentLayout: root.currentLayout
            viewMode: root.viewMode
            onViewModeRequested: (mode) => {
                root.viewMode = mode;
                layoutGrid.currentIndex = -1;
                layoutGrid.rebuildModel();
                layoutGrid.selectDefaultLayout(mode);
            }
            onRequestDeleteLayout: (layout) => {
                deleteConfirmDialog.layoutToDelete = layout;
                deleteConfirmDialog.open();
            }
            onRequestImportLayout: importDialog.open()
            onRequestExportLayout: (layoutId) => {
                exportDialog.layoutId = layoutId;
                exportDialog.open();
            }
        }

        // Layout grid
        GridView {
            id: layoutGrid

            // Responsive cell sizing - aim for 2-4 columns
            readonly property real minCellWidth: Kirigami.Units.gridUnit * 14
            readonly property int columnCount: Math.max(2, Math.floor(width / minCellWidth))
            readonly property real actualCellWidth: width / columnCount

            function _extractIds(list) {
                let ids = [];
                for (let i = 0; i < list.length; i++) {
                    ids.push(String(list[i].id ?? ""));
                }
                return ids;
            }

            function rebuildModel() {
                let allLayouts = root.kcmModule ? root.kcmModule.layouts : [];
                // Filter by view mode
                let newLayouts = [];
                for (let i = 0; i < allLayouts.length; i++) {
                    let isAutotile = allLayouts[i].isAutotile === true;
                    if (root.viewMode === 0 && !isAutotile)
                        newLayouts.push(allLayouts[i]);
                    else if (root.viewMode === 1 && isAutotile)
                        newLayouts.push(allLayouts[i]);
                }
                // Compare by ID list -- skip swap if order hasn't changed
                let oldIds = _extractIds(model);
                let newIds = _extractIds(newLayouts);
                if (oldIds.length === newIds.length) {
                    let same = true;
                    for (let i = 0; i < oldIds.length; i++) {
                        if (oldIds[i] !== newIds[i]) {
                            same = false;
                            break;
                        }
                    }
                    if (same) {
                        for (let i = 0; i < newLayouts.length; i++) {
                            model[i] = newLayouts[i];
                        }
                        return ;
                    }
                }
                // ID list actually changed -- full swap
                model = newLayouts;
            }

            function selectDefaultLayout(mode) {
                let defaultId = (mode === 1) ? ("autotile:" + root.kcmModule.autotileAlgorithm) : root.kcmModule.defaultLayoutId;
                if (defaultId)
                    Qt.callLater(() => {
                    return selectLayoutById(defaultId);
                });

            }

            function selectLayoutById(layoutId) {
                if (!layoutId || !model)
                    return false;

                for (let i = 0; i < model.length; i++) {
                    if (model[i] && String(model[i].id) === String(layoutId)) {
                        currentIndex = i;
                        positionViewAtIndex(i, GridView.Contain);
                        return true;
                    }
                }
                return false;
            }

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: root.layoutListMinHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            focus: true
            keyNavigationEnabled: true
            model: []
            cellWidth: actualCellWidth
            cellHeight: Kirigami.Units.gridUnit * 12
            Component.onCompleted: {
                rebuildModel();
                let layoutId = root.kcmModule.layoutToSelect;
                if (layoutId)
                    Qt.callLater(() => {
                    if (count > 0)
                        selectLayoutById(layoutId);

                });
                else
                    selectDefaultLayout(root.viewMode);
            }

            Connections {
                function onLayoutsChanged() {
                    layoutGrid.rebuildModel();
                }

                target: root.kcmModule ?? null
            }

            // Background
            Rectangle {
                anchors.fill: parent
                z: -1
                color: Kirigami.Theme.backgroundColor
                border.color: Kirigami.Theme.disabledTextColor
                border.width: 1
                radius: Kirigami.Units.smallSpacing
            }

            Connections {
                function onLayoutToSelectChanged() {
                    let layoutId = root.kcmModule.layoutToSelect;
                    if (layoutId)
                        Qt.callLater(() => {
                        return layoutGrid.selectLayoutById(layoutId);
                    });

                }

                target: root.kcmModule
            }

            // Empty state
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: layoutGrid.count === 0
                text: root.viewMode === 1 ? i18n("No autotile algorithms available") : i18n("No layouts available")
                explanation: root.viewMode === 1 ? i18n("Enable autotiling to use tiling algorithms") : i18n("Start the PlasmaZones daemon or create a new layout")
            }

            delegate: LayoutGridDelegate {
                kcm: root.kcmModule
                viewMode: root.viewMode
                cellWidth: layoutGrid.cellWidth
                cellHeight: layoutGrid.cellHeight
                isSelected: GridView.isCurrentItem
                onSelected: (idx) => {
                    return layoutGrid.currentIndex = idx;
                }
                onActivated: (layoutId) => {
                    return root.kcmModule.editLayout(layoutId);
                }
                onDeleteRequested: (layout) => {
                    deleteConfirmDialog.layoutToDelete = layout;
                    deleteConfirmDialog.open();
                }
            }

        }

        // Open Layouts Folder link
        Button {
            Layout.alignment: Qt.AlignLeft
            text: i18n("Open Layouts Folder")
            icon.name: "folder-open"
            flat: true
            visible: root.viewMode === 0
            onClicked: Qt.openUrlExternally("file://" + StandardPaths.writableLocation(StandardPaths.GenericDataLocation) + "/plasmazones/layouts")
        }

    }

    // Import file dialog
    FileDialog {
        id: importDialog

        title: i18n("Import Layout")
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: root.kcmModule.importLayout(selectedFile.toString().replace(/^file:\/\/+/, "/"))
    }

    // Export file dialog
    FileDialog {
        id: exportDialog

        property string layoutId: ""

        title: i18n("Export Layout")
        nameFilters: ["JSON files (*.json)"]
        fileMode: FileDialog.SaveFile
        onAccepted: root.kcmModule.exportLayout(layoutId, selectedFile.toString().replace(/^file:\/\/+/, "/"))
    }

    // Delete confirmation dialog
    Kirigami.PromptDialog {
        id: deleteConfirmDialog

        property var layoutToDelete: null

        title: i18n("Delete Layout")
        subtitle: layoutToDelete ? i18n("Are you sure you want to delete \"%1\"?", layoutToDelete.name) : ""
        standardButtons: Kirigami.Dialog.NoButton
        onRejected: layoutToDelete = null
        onClosed: layoutToDelete = null
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    if (deleteConfirmDialog.layoutToDelete) {
                        root.kcmModule.deleteLayout(deleteConfirmDialog.layoutToDelete.id);
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
