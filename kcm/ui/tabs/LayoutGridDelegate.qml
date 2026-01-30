// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Grid delegate for displaying a single layout card
 *
 * Single Responsibility: Render a layout card with thumbnail, info, and interaction.
 */
Item {
    id: root

    required property var modelData
    required property int index
    required property var kcm
    required property real cellWidth
    required property real cellHeight

    // Selection state (bound from parent GridView)
    property bool isSelected: false
    property bool isHovered: false

    // Signals
    signal selected(int index)
    signal activated(string layoutId)
    signal deleteRequested(var layout)

    width: cellWidth
    height: cellHeight

    Accessible.name: modelData.name || i18n("Unnamed Layout")
    Accessible.description: modelData.isSystem
        ? i18n("System layout with %1 zones", modelData.zoneCount || 0)
        : i18n("Custom layout with %1 zones", modelData.zoneCount || 0)
    Accessible.role: Accessible.ListItem

    HoverHandler {
        id: hoverHandler
        onHoveredChanged: root.isHovered = hovered
    }

    TapHandler {
        onTapped: root.selected(root.index)
        onDoubleTapped: root.activated(root.modelData.id)
    }

    Keys.onReturnPressed: root.activated(root.modelData.id)
    Keys.onDeletePressed: {
        if (!root.modelData.isSystem) {
            root.deleteRequested(root.modelData)
        }
    }

    Rectangle {
        id: cardBackground
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing / 2
        radius: Kirigami.Units.smallSpacing

        color: root.isSelected ? Kirigami.Theme.highlightColor :
               root.isHovered ? Qt.rgba(Kirigami.Theme.highlightColor.r,
                                        Kirigami.Theme.highlightColor.g,
                                        Kirigami.Theme.highlightColor.b, 0.2) :
               "transparent"

        border.color: root.isSelected ? Kirigami.Theme.highlightColor :
                      root.isHovered ? Kirigami.Theme.disabledTextColor :
                      "transparent"
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing / 2

            // Thumbnail area
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                LayoutThumbnail {
                    id: layoutThumbnail
                    anchors.centerIn: parent
                    layout: root.modelData
                    isSelected: root.isSelected
                    scale: (implicitWidth > 0 && implicitHeight > 0 && parent.width > 0 && parent.height > 0)
                           ? Math.min(1, Math.min(parent.width / implicitWidth, parent.height / implicitHeight))
                           : 1
                    transformOrigin: Item.Center
                }

                // Default layout indicator
                Kirigami.Icon {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: Kirigami.Units.smallSpacing
                    source: "favorite"
                    visible: root.modelData.id === root.kcm.defaultLayoutId
                    width: Kirigami.Units.iconSizes.small
                    height: Kirigami.Units.iconSizes.small
                    color: Kirigami.Theme.positiveTextColor

                    HoverHandler { id: defaultIconHover }
                    ToolTip.visible: defaultIconHover.hovered
                    ToolTip.text: i18n("Default layout")
                }
            }

            // Info row
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                color: root.isSelected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor

                text: {
                    var zoneCount = root.modelData.zoneCount || 0
                    if (root.modelData.isSystem) {
                        return i18n("System • %1", zoneCount)
                    } else {
                        return i18n("%1 zones", zoneCount)
                    }
                }
            }
        }
    }
}
