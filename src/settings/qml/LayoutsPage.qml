// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

Flickable {
    id: root

    // Capture the context property so child components can access it via root.kcmModule
    readonly property var kcmModule: kcm
    // Inline constants matching monolith's Constants.qml
    readonly property int layoutListMinHeight: Kirigami.Units.gridUnit * 20
    // View mode: 0 = Snapping Layouts, 1 = Auto Tile Algorithms
    property int viewMode: 0

    contentHeight: content.implicitHeight
    clip: true

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
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ─── Toolbar ─────────────────────────────────────────────────────────
        LayoutToolbar {
            Layout.fillWidth: true
            kcm: root.kcmModule
            viewMode: root.viewMode
            onRequestCreateNewLayout: settingsController.createNewLayout()
            onRequestImportLayout: importDialog.open()
            onRequestOpenLayoutsFolder: settingsController.openLayoutsFolder()
            onViewModeRequested: (mode) => {
                root.viewMode = mode;
                layoutGrid.currentIndex = -1;
                layoutGrid.rebuildModel();
                layoutGrid.selectDefaultLayout(mode);
            }
        }

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
                        return layoutContextMenu.layoutId !== ("autotile:" + root.kcmModule.autotileAlgorithm);

                    return layoutContextMenu.layoutId !== root.kcmModule.defaultLayoutId;
                }
                onTriggered: {
                    if (root.viewMode === 1)
                        root.kcmModule.autotileAlgorithm = layoutContextMenu.layoutId.replace("autotile:", "");
                    else
                        root.kcmModule.defaultLayoutId = layoutContextMenu.layoutId;
                }
            }

            MenuItem {
                // root.kcmModule.setLayoutHidden(layoutContextMenu.layoutId, !(layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector))

                text: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? i18n("Show in Zone Selector") : i18n("Hide from Zone Selector")
                icon.name: layoutContextMenu.layout && layoutContextMenu.layout.hiddenFromSelector ? "view-visible" : "view-hidden"
                // TODO: setLayoutHidden not yet on SettingsController
                onTriggered: {
                }
            }

            MenuItem {
                // root.kcmModule.setLayoutAutoAssign(layoutContextMenu.layoutId, !(layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true))

                text: layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true ? i18n("Disable Auto-assign") : i18n("Enable Auto-assign")
                icon.name: layoutContextMenu.layout && layoutContextMenu.layout.autoAssign === true ? "window-duplicate" : "window-new"
                visible: !layoutContextMenu.isAutotile
                // TODO: setLayoutAutoAssign not yet on SettingsController
                onTriggered: {
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

        // ─── Layout Grid ───────────────────────────────────────────────────
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
                let allLayouts = settingsController.layouts;
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
            Layout.preferredHeight: Math.max(root.layoutListMinHeight, contentHeight)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            focus: true
            keyNavigationEnabled: true
            model: []
            cellWidth: actualCellWidth
            cellHeight: Kirigami.Units.gridUnit * 12
            Component.onCompleted: {
                rebuildModel();
                selectDefaultLayout(root.viewMode);
            }

            Connections {
                function onLayoutsChanged() {
                    layoutGrid.rebuildModel();
                }

                target: settingsController
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

            // Empty state
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: layoutGrid.count === 0
                text: root.viewMode === 1 ? i18n("No autotile algorithms available") : i18n("No layouts available")
                explanation: root.viewMode === 1 ? i18n("Enable autotiling to use tiling algorithms") : i18n("Start the PlasmaZones daemon or create a new layout")
            }

            // ─── Layout Grid Delegate ────────────────────────────────────
            delegate: LayoutGridDelegate {
                kcm: root.kcmModule
                cellWidth: layoutGrid.cellWidth
                cellHeight: layoutGrid.cellHeight
                viewMode: root.viewMode
                isSelected: GridView.isCurrentItem
                onSelected: (idx) => {
                    layoutGrid.currentIndex = idx;
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
