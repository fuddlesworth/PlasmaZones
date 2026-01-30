// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Layouts tab - View, create, edit, import/export layouts
 */
ColumnLayout {
    id: root

    required property var kcm
    required property QtObject constants

    // Signals for dialog interactions (handled by main.qml)
    signal requestDeleteLayout(var layout)
    signal requestImportLayout()
    signal requestExportLayout(string layoutId)

    spacing: Kirigami.Units.largeSpacing

    // Layout actions toolbar
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Button {
            text: i18n("New Layout")
            icon.name: "list-add"
            onClicked: kcm.createNewLayout()
        }

        Button {
            text: i18n("Edit")
            icon.name: "document-edit"
            enabled: layoutList.currentItem !== null
            onClicked: {
                if (layoutList.currentItem) {
                    kcm.editLayout(layoutList.currentItem.modelData.id)
                }
            }
        }

        Button {
            text: i18n("Duplicate")
            icon.name: "edit-copy"
            enabled: layoutList.currentItem !== null
            onClicked: {
                if (layoutList.currentItem) {
                    kcm.duplicateLayout(layoutList.currentItem.modelData.id)
                }
            }
        }

        Button {
            text: i18n("Delete")
            icon.name: "edit-delete"
            enabled: layoutList.currentItem !== null && !layoutList.currentItem.modelData.isSystem
            onClicked: {
                if (layoutList.currentItem) {
                    root.requestDeleteLayout(layoutList.currentItem.modelData)
                }
            }
            ToolTip.visible: hovered
            ToolTip.text: i18n("Delete the selected layout")
        }

        Button {
            text: i18n("Set as Default")
            icon.name: "favorite"
            enabled: layoutList.currentItem !== null && layoutList.currentItem.modelData.id !== kcm.defaultLayoutId
            onClicked: {
                if (layoutList.currentItem) {
                    kcm.defaultLayoutId = layoutList.currentItem.modelData.id
                }
            }
            ToolTip.visible: hovered
            ToolTip.text: i18n("Set this layout as the default for screens without specific assignments")
        }

        Item { Layout.fillWidth: true }

        Button {
            text: i18n("Import")
            icon.name: "document-import"
            onClicked: root.requestImportLayout()
        }

        Button {
            text: i18n("Export")
            icon.name: "document-export"
            enabled: layoutList.currentItem !== null
            onClicked: {
                if (layoutList.currentItem) {
                    root.requestExportLayout(layoutList.currentItem.modelData.id)
                }
            }
        }
    }

    // Layout grid - fills remaining space with responsive columns
    GridView {
        id: layoutList
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: root.constants.layoutListMinHeight
        model: kcm.layouts
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        focus: true
        keyNavigationEnabled: true

        // Calculate responsive cell size - aim for 2-4 columns
        readonly property real minCellWidth: Kirigami.Units.gridUnit * 14
        readonly property real maxCellWidth: Kirigami.Units.gridUnit * 22
        readonly property int columnCount: Math.max(2, Math.floor(width / minCellWidth))
        readonly property real actualCellWidth: width / columnCount

        cellWidth: actualCellWidth
        cellHeight: Kirigami.Units.gridUnit * 10

        // Function to select layout by ID
        function selectLayoutById(layoutId) {
            if (!layoutId || !kcm.layouts) {
                return false
            }

            for (let i = 0; i < kcm.layouts.length; i++) {
                const layout = kcm.layouts[i]
                if (layout && String(layout.id) === String(layoutId)) {
                    currentIndex = i
                    positionViewAtIndex(i, GridView.Contain)
                    return true
                }
            }
            return false
        }

        Connections {
            target: kcm
            function onLayoutToSelectChanged() {
                if (kcm.layoutToSelect) {
                    Qt.callLater(() => {
                        layoutList.selectLayoutById(kcm.layoutToSelect)
                    })
                }
            }
        }

        onCountChanged: {
            if (kcm.layoutToSelect && count > 0) {
                layoutList.selectLayoutById(kcm.layoutToSelect)
            }
        }

        Component.onCompleted: {
            Qt.callLater(() => {
                if (kcm.layoutToSelect && count > 0) {
                    selectLayoutById(kcm.layoutToSelect)
                }
            })
        }

        Rectangle {
            anchors.fill: parent
            z: -1
            color: Kirigami.Theme.backgroundColor
            border.color: Kirigami.Theme.disabledTextColor
            border.width: 1
            radius: Kirigami.Units.smallSpacing
        }

        delegate: Item {
            id: layoutDelegate
            width: layoutList.cellWidth
            height: layoutList.cellHeight

            required property var modelData
            required property int index

            property bool isSelected: GridView.isCurrentItem
            property bool isHovered: hoverHandler.hovered

            Accessible.name: modelData.name || i18n("Unnamed Layout")
            Accessible.description: modelData.isSystem
                ? i18n("System layout with %1 zones", modelData.zoneCount || 0)
                : i18n("Custom layout with %1 zones", modelData.zoneCount || 0)
            Accessible.role: Accessible.ListItem

            HoverHandler {
                id: hoverHandler
            }

            TapHandler {
                onTapped: layoutList.currentIndex = index
                onDoubleTapped: {
                    kcm.editLayout(modelData.id)
                }
            }

            Keys.onReturnPressed: {
                kcm.editLayout(modelData.id)
            }
            Keys.onDeletePressed: {
                if (!modelData.isSystem) {
                    root.requestDeleteLayout(modelData)
                }
            }

            Rectangle {
                id: cardBackground
                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing / 2
                radius: Kirigami.Units.smallSpacing
                color: layoutDelegate.isSelected ? Kirigami.Theme.highlightColor :
                       layoutDelegate.isHovered ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2) :
                       "transparent"
                border.color: layoutDelegate.isSelected ? Kirigami.Theme.highlightColor :
                              layoutDelegate.isHovered ? Kirigami.Theme.disabledTextColor :
                              "transparent"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing / 2

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        LayoutThumbnail {
                            id: layoutThumbnail
                            anchors.centerIn: parent
                            layout: modelData
                            isSelected: layoutDelegate.isSelected
                            scale: (implicitWidth > 0 && implicitHeight > 0 && parent.width > 0 && parent.height > 0)
                                   ? Math.min(1, Math.min(parent.width / implicitWidth, parent.height / implicitHeight))
                                   : 1
                            transformOrigin: Item.Center
                        }

                        Kirigami.Icon {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: Kirigami.Units.smallSpacing
                            source: "favorite"
                            visible: modelData.id === kcm.defaultLayoutId
                            width: Kirigami.Units.iconSizes.small
                            height: Kirigami.Units.iconSizes.small
                            color: Kirigami.Theme.positiveTextColor

                            HoverHandler { id: defaultIconHover }
                            ToolTip.visible: defaultIconHover.hovered
                            ToolTip.text: i18n("Default layout")
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing / 2

                        Label {
                            text: {
                                var zoneCount = modelData.zoneCount || 0
                                if (modelData.isSystem) {
                                    return i18n("System • %1", zoneCount)
                                } else {
                                    return i18n("%1 zones", zoneCount)
                                }
                            }
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            color: layoutDelegate.isSelected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                        }
                    }
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: layoutList.count === 0
            text: i18n("No layouts available")
            explanation: i18n("Start the PlasmaZones daemon or create a new layout")
        }
    }
}
