// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property int layoutListMinHeight: Kirigami.Units.gridUnit * 20
    // View mode: 0 = Snapping Layouts, 1 = Auto Tile Algorithms
    property int viewMode: 0

    contentHeight: content.implicitHeight
    clip: true

    // Reset to Snapping Layouts when autotiling is disabled
    Connections {
        function onAutotileEnabledChanged() {
            if (!kcm.autotileEnabled && root.viewMode !== 0) {
                root.viewMode = 0;
                layoutGrid.currentIndex = -1;
                layoutGrid.rebuildModel();
                layoutGrid.selectDefaultLayout(0);
            }
        }

        target: kcm
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ─── Toolbar ─────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Button {
                text: i18n("Create New")
                icon.name: "list-add"
                visible: root.viewMode === 0
                onClicked: settingsController.createNewLayout()
            }

            Button {
                text: i18n("Launch Editor")
                icon.name: "document-edit"
                onClicked: settingsController.launchEditor()
            }

            Button {
                text: i18n("Import")
                icon.name: "document-import"
                visible: root.viewMode === 0
                onClicked: importDialog.open()
            }

            Button {
                text: i18n("Open Folder")
                icon.name: "folder-open"
                onClicked: settingsController.openLayoutsFolder()
            }

            Item {
                Layout.fillWidth: true
            }

            // View mode selector (visible when autotiling is enabled)
            ComboBox {
                visible: kcm.autotileEnabled
                model: [i18n("Snapping Layouts"), i18n("Autotile Algorithms")]
                currentIndex: root.viewMode
                onActivated: (index) => {
                    root.viewMode = index;
                    layoutGrid.currentIndex = -1;
                    layoutGrid.rebuildModel();
                    layoutGrid.selectDefaultLayout(index);
                }
            }

        }

        // ─── Context Menu ────────────────────────────────────────────────
        Menu {
            id: layoutContextMenu

            property var layout: null
            readonly property bool isAutotile: layout && layout.isAutotile === true
            readonly property string layoutId: layout ? (layout.id || "") : ""

            function showForLayout(layout) {
                layoutContextMenu.layout = layout;
                layoutContextMenu.popup();
            }

            MenuItem {
                text: i18n("Edit")
                icon.name: "document-edit"
                onTriggered: settingsController.editLayout(layoutContextMenu.layoutId)
            }

            MenuItem {
                text: i18n("Set as Default")
                icon.name: "favorite"
                enabled: {
                    if (!layoutContextMenu.layout)
                        return false;

                    if (root.viewMode === 1)
                        return layoutContextMenu.layoutId !== ("autotile:" + kcm.autotileAlgorithm);

                    return layoutContextMenu.layoutId !== kcm.defaultLayoutId;
                }
                onTriggered: {
                    if (root.viewMode === 1)
                        kcm.autotileAlgorithm = layoutContextMenu.layoutId.replace("autotile:", "");
                    else
                        kcm.defaultLayoutId = layoutContextMenu.layoutId;
                }
            }

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

        }

        // ─── Layout Grid ─────────────────────────────────────────────────
        GridView {
            id: layoutGrid

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
                let newLayouts = [];
                for (let i = 0; i < allLayouts.length; i++) {
                    let isAutotile = allLayouts[i].isAutotile === true;
                    if (root.viewMode === 0 && !isAutotile)
                        newLayouts.push(allLayouts[i]);
                    else if (root.viewMode === 1 && isAutotile)
                        newLayouts.push(allLayouts[i]);
                }
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
                model = newLayouts;
            }

            function selectDefaultLayout(mode) {
                let defaultId = (mode === 1) ? ("autotile:" + kcm.autotileAlgorithm) : kcm.defaultLayoutId;
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

            // ─── Inline Delegate ─────────────────────────────────────
            delegate: Item {
                id: delegateRoot

                required property var modelData
                required property int index
                readonly property string layoutId: modelData.id || ""
                readonly property string layoutName: modelData.name || i18n("Untitled")
                readonly property int zoneCount: modelData.zoneCount || 0
                readonly property bool isDefault: {
                    if (root.viewMode === 1)
                        return layoutId === ("autotile:" + kcm.autotileAlgorithm);

                    return layoutId === kcm.defaultLayoutId;
                }
                readonly property bool isSystem: modelData.isSystem === true
                readonly property bool isAutotile: modelData.isAutotile === true
                readonly property bool isSelected: GridView.isCurrentItem
                readonly property var zones: modelData.zones || []

                width: layoutGrid.cellWidth
                height: layoutGrid.cellHeight

                // Card background
                Rectangle {
                    id: cardBg

                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    radius: Kirigami.Units.smallSpacing
                    color: delegateRoot.isSelected ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15) : (delegateMouseArea.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.05) : "transparent")
                    border.color: delegateRoot.isSelected ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                    border.width: delegateRoot.isSelected ? 2 : 1

                    MouseArea {
                        id: delegateMouseArea

                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: (mouse) => {
                            layoutGrid.currentIndex = delegateRoot.index;
                            if (mouse.button === Qt.RightButton)
                                layoutContextMenu.showForLayout(delegateRoot.modelData);

                        }
                        onDoubleClicked: settingsController.editLayout(delegateRoot.layoutId)
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing

                        // Zone preview
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            // Simple zone preview rectangles
                            Repeater {
                                model: delegateRoot.zones

                                Rectangle {
                                    required property var modelData

                                    x: (modelData.x || 0) * parent.width
                                    y: (modelData.y || 0) * parent.height
                                    width: (modelData.width || 0) * parent.width
                                    height: (modelData.height || 0) * parent.height
                                    color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2)
                                    border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.6)
                                    border.width: 1
                                    radius: 2
                                }

                            }

                            // Fallback when no zones
                            Rectangle {
                                anchors.fill: parent
                                visible: delegateRoot.zones.length === 0
                                color: "transparent"
                                border.color: Kirigami.Theme.disabledTextColor
                                border.width: 1
                                radius: 2

                                Label {
                                    anchors.centerIn: parent
                                    text: delegateRoot.isAutotile ? delegateRoot.layoutName : i18n("No zones")
                                    opacity: 0.5
                                    font: Kirigami.Theme.smallFont
                                }

                            }

                        }

                        // Name + info row
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: "starred-symbolic"
                                visible: delegateRoot.isDefault
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                                color: Kirigami.Theme.positiveTextColor
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0

                                Label {
                                    Layout.fillWidth: true
                                    text: delegateRoot.layoutName
                                    font.bold: delegateRoot.isDefault
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: {
                                        let parts = [];
                                        if (delegateRoot.zoneCount > 0)
                                            parts.push(i18n("%1 zone(s)", delegateRoot.zoneCount));

                                        if (delegateRoot.isSystem)
                                            parts.push(i18n("Built-in"));

                                        if (delegateRoot.isAutotile)
                                            parts.push(i18n("Autotile"));

                                        return parts.join(" \u00b7 ");
                                    }
                                    font: Kirigami.Theme.smallFont
                                    opacity: 0.7
                                    elide: Text.ElideRight
                                }

                            }

                        }

                        // Action buttons
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Button {
                                text: i18n("Edit")
                                icon.name: "document-edit"
                                flat: true
                                visible: !delegateRoot.isAutotile
                                onClicked: settingsController.editLayout(delegateRoot.layoutId)
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Button {
                                icon.name: "edit-copy"
                                flat: true
                                visible: !delegateRoot.isAutotile
                                ToolTip.text: i18n("Duplicate")
                                ToolTip.visible: hovered
                                onClicked: settingsController.duplicateLayout(delegateRoot.layoutId)
                            }

                            Button {
                                icon.name: "edit-delete"
                                flat: true
                                visible: !delegateRoot.isSystem && !delegateRoot.isAutotile
                                ToolTip.text: i18n("Delete")
                                ToolTip.visible: hovered
                                onClicked: {
                                    deleteConfirmDialog.layoutToDelete = delegateRoot.modelData;
                                    deleteConfirmDialog.open();
                                }
                            }

                        }

                    }

                }

            }

        }

        // ─── Default Layout Info ─────────────────────────────────────────
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Default Layout")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Current default:")
                    }

                    Label {
                        text: kcm.defaultLayoutId || i18n("(none)")
                        font.bold: true
                    }

                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: i18n("The default layout is applied to any screen or virtual desktop that does not have a specific assignment. Right-click a layout and choose \"Set as Default\" to change.")
                    opacity: 0.7
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
            // TODO: importLayout not yet on SettingsController
            settingsController.openLayoutsFolder();
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
            // TODO: exportLayout not yet on SettingsController
            settingsController.openLayoutsFolder();
        }
    }

    // Delete confirmation dialog
    Dialog {
        id: deleteConfirmDialog

        property var layoutToDelete: null

        anchors.centerIn: parent
        title: i18n("Delete Layout")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: {
            if (layoutToDelete) {
                settingsController.deleteLayout(layoutToDelete.id);
                layoutToDelete = null;
            }
        }
        onRejected: layoutToDelete = null

        Label {
            text: deleteConfirmDialog.layoutToDelete ? i18n("Are you sure you want to delete \"%1\"?", deleteConfirmDialog.layoutToDelete.name || "") : ""
            wrapMode: Text.WordWrap
        }

    }

}
