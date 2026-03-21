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

        // ─── Toolbar (inlined from LayoutToolbar) ──────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            // New Layout -- only in Snapping view
            Button {
                visible: root.viewMode === 0
                text: i18n("New Layout")
                icon.name: "list-add"
                onClicked: settingsController.createNewLayout()
            }

            // Import -- only in Snapping view
            Button {
                visible: root.viewMode === 0
                text: i18n("Import")
                icon.name: "document-import"
                onClicked: importDialog.open()
            }

            // Open Layouts Folder -- only in Snapping view
            Button {
                visible: root.viewMode === 0
                text: i18n("Open Folder")
                icon.name: "folder-open"
                flat: true
                onClicked: settingsController.openLayoutsFolder()
            }

            Item {
                Layout.fillWidth: true
            }

            // View switcher -- only visible when autotiling is enabled
            ComboBox {
                visible: root.kcmModule.autotileEnabled
                model: [i18n("Snapping"), i18n("Tiling")]
                currentIndex: root.viewMode
                onActivated: (index) => {
                    root.viewMode = index;
                    layoutGrid.currentIndex = -1;
                    layoutGrid.rebuildModel();
                    layoutGrid.selectDefaultLayout(index);
                }
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

            // ─── Inline LayoutGridDelegate ──────────────────────────────
            delegate: Item {
                id: delegateRoot

                required property var modelData
                required property int index
                // The full autotile default ID including prefix, for comparison
                readonly property string autotileDefaultId: "autotile:" + root.kcmModule.autotileAlgorithm
                // Selection state (bound from parent GridView)
                property bool isSelected: GridView.isCurrentItem
                property bool isHovered: false

                width: layoutGrid.cellWidth
                height: layoutGrid.cellHeight
                Accessible.name: modelData.name || i18n("Unnamed Layout")
                Accessible.description: i18n("Layout with %1 zones", modelData.zoneCount || 0)
                Accessible.role: Accessible.ListItem
                Keys.onReturnPressed: settingsController.editLayout(delegateRoot.modelData.id)
                Keys.onDeletePressed: {
                    if (!delegateRoot.modelData.isSystem && !delegateRoot.modelData.isAutotile) {
                        deleteConfirmDialog.layoutToDelete = delegateRoot.modelData;
                        deleteConfirmDialog.open();
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    onContainsMouseChanged: delegateRoot.isHovered = containsMouse
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            layoutGrid.currentIndex = delegateRoot.index;
                            layoutContextMenu.showForLayout(delegateRoot.modelData);
                        } else {
                            layoutGrid.currentIndex = delegateRoot.index;
                        }
                    }
                    onDoubleClicked: (mouse) => {
                        if (mouse.button === Qt.LeftButton)
                            settingsController.editLayout(delegateRoot.modelData.id);

                    }
                }

                Rectangle {
                    id: cardBackground

                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing / 2
                    radius: Kirigami.Units.smallSpacing
                    color: delegateRoot.isSelected ? Kirigami.Theme.highlightColor : delegateRoot.isHovered ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2) : "transparent"
                    border.color: delegateRoot.isSelected ? Kirigami.Theme.highlightColor : delegateRoot.isHovered ? Kirigami.Theme.disabledTextColor : "transparent"
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing / 2

                        // Thumbnail area (using shared LayoutThumbnail, matching KCM)
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            // Dim thumbnail when hidden
                            opacity: delegateRoot.modelData.hiddenFromSelector ? 0.5 : 1

                            LayoutThumbnail {
                                id: layoutThumbnail

                                // Safe scale calculation - fit thumbnail within parent bounds
                                readonly property real safeImplicitWidth: Math.max(1, implicitWidth)
                                readonly property real safeImplicitHeight: Math.max(1, implicitHeight)
                                readonly property real safeParentWidth: Math.max(1, parent.width)
                                readonly property real safeParentHeight: Math.max(1, parent.height)

                                anchors.centerIn: parent
                                layout: delegateRoot.modelData
                                isSelected: delegateRoot.isSelected
                                fontFamily: kcm.labelFontFamily || ""
                                fontSizeScale: kcm.labelFontSizeScale || 1
                                fontWeight: kcm.labelFontWeight !== undefined ? kcm.labelFontWeight : Font.Bold
                                fontItalic: kcm.labelFontItalic || false
                                fontUnderline: kcm.labelFontUnderline || false
                                fontStrikeout: kcm.labelFontStrikeout || false
                                transformOrigin: Item.Center
                                scale: Math.min(1, safeParentWidth / safeImplicitWidth, safeParentHeight / safeImplicitHeight)
                            }

                            // Top-left indicator row (default star + system badge)
                            Row {
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.margins: Kirigami.Units.smallSpacing
                                spacing: Kirigami.Units.smallSpacing / 2

                                Kirigami.Icon {
                                    id: defaultIcon

                                    source: "favorite"
                                    visible: root.viewMode === 1 ? delegateRoot.modelData.id === delegateRoot.autotileDefaultId : delegateRoot.modelData.id === root.kcmModule.defaultLayoutId
                                    width: Kirigami.Units.iconSizes.small
                                    height: Kirigami.Units.iconSizes.small
                                    color: Kirigami.Theme.positiveTextColor
                                    ToolTip.visible: defaultIconHover.hovered
                                    ToolTip.text: root.viewMode === 1 ? i18n("Default autotile algorithm") : i18n("Default layout")

                                    HoverHandler {
                                        id: defaultIconHover
                                    }

                                }

                                Kirigami.Icon {
                                    source: delegateRoot.modelData.isSystem ? "lock" : "document-edit"
                                    visible: delegateRoot.modelData.isSystem === true || delegateRoot.modelData.hasSystemOrigin === true
                                    width: Kirigami.Units.iconSizes.small
                                    height: Kirigami.Units.iconSizes.small
                                    color: Kirigami.Theme.disabledTextColor
                                    ToolTip.visible: systemIconHover.hovered
                                    ToolTip.text: delegateRoot.modelData.isSystem ? i18n("System layout (read-only)") : i18n("Modified system layout")

                                    HoverHandler {
                                        id: systemIconHover
                                    }

                                }

                                Kirigami.Icon {
                                    source: "view-filter"
                                    visible: {
                                        var d = delegateRoot.modelData;
                                        var s = d.allowedScreens;
                                        var k = d.allowedDesktops;
                                        var a = d.allowedActivities;
                                        return (s !== undefined && s !== null && s.length > 0) || (k !== undefined && k !== null && k.length > 0) || (a !== undefined && a !== null && a.length > 0);
                                    }
                                    width: Kirigami.Units.iconSizes.small
                                    height: Kirigami.Units.iconSizes.small
                                    color: Kirigami.Theme.disabledTextColor
                                    ToolTip.visible: filterIconHover.hovered
                                    ToolTip.text: i18n("This layout is restricted to specific screens, desktops, or activities")

                                    HoverHandler {
                                        id: filterIconHover
                                    }

                                }

                            }

                            // Top-right toggle buttons
                            Row {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: Kirigami.Units.smallSpacing / 2
                                spacing: 0

                                // Auto-assign toggle (hidden for autotile)
                                ToolButton {
                                    width: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                                    height: width
                                    padding: 0
                                    visible: delegateRoot.modelData.isAutotile !== true && (delegateRoot.isHovered || delegateRoot.modelData.autoAssign === true)
                                    icon.name: delegateRoot.modelData.autoAssign === true ? "window-duplicate" : "window-new"
                                    icon.width: Kirigami.Units.iconSizes.small
                                    icon.height: Kirigami.Units.iconSizes.small
                                    icon.color: delegateRoot.modelData.autoAssign === true ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                                    // TODO: setLayoutAutoAssign not yet on SettingsController
                                    onClicked: {
                                    }
                                    // root.kcmModule.setLayoutAutoAssign(delegateRoot.modelData.id, !(delegateRoot.modelData.autoAssign === true))
                                    ToolTip.visible: hovered
                                    ToolTip.text: delegateRoot.modelData.autoAssign === true ? i18n("Auto-assign enabled: new windows fill empty zones. Click to disable.") : i18n("Click to auto-assign new windows to empty zones")
                                }

                                // Visibility toggle
                                ToolButton {
                                    width: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                                    height: width
                                    padding: 0
                                    visible: delegateRoot.isHovered || delegateRoot.modelData.hiddenFromSelector === true
                                    icon.name: delegateRoot.modelData.hiddenFromSelector ? "view-hidden" : "view-visible"
                                    icon.width: Kirigami.Units.iconSizes.small
                                    icon.height: Kirigami.Units.iconSizes.small
                                    icon.color: delegateRoot.modelData.hiddenFromSelector ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.textColor
                                    // TODO: setLayoutHidden not yet on SettingsController
                                    onClicked: {
                                    }
                                    // root.kcmModule.setLayoutHidden(delegateRoot.modelData.id, !delegateRoot.modelData.hiddenFromSelector)
                                    ToolTip.visible: hovered
                                    ToolTip.text: delegateRoot.modelData.hiddenFromSelector ? i18n("Hidden from zone selector. Click to show.") : i18n("Visible in zone selector. Click to hide.")
                                }

                            }

                        }

                        // Info row with category badge
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: Kirigami.Units.smallSpacing

                            QFZCommon.CategoryBadge {
                                visible: delegateRoot.modelData.category !== undefined
                                category: delegateRoot.modelData.category !== undefined ? delegateRoot.modelData.category : 0
                                autoAssign: delegateRoot.modelData.autoAssign === true
                            }

                            Label {
                                elide: Text.ElideRight
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                color: delegateRoot.isSelected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                                text: i18n("%1 zones", delegateRoot.modelData.zoneCount || 0)
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
