// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon
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

    // Helper to check if this is an autotile algorithm
    readonly property bool isAutotile: modelData.category === 1

    // Signals
    signal selected(int index)
    signal activated(string layoutId)
    signal deleteRequested(var layout)

    width: cellWidth
    height: cellHeight

    Accessible.name: modelData.name || i18n("Unnamed Layout")
    Accessible.description: isAutotile
        ? i18n("Autotile algorithm with dynamic zones")
        : modelData.isSystem
            ? i18n("System layout with %1 zones", modelData.zoneCount || 0)
            : i18n("Custom layout with %1 zones", modelData.zoneCount || 0)
    Accessible.role: Accessible.ListItem

    HoverHandler {
        id: hoverHandler
        onHoveredChanged: root.isHovered = hovered
    }

    TapHandler {
        onTapped: root.selected(root.index)
        onDoubleTapped: {
            // Only allow editing manual layouts, not autotile algorithms
            if (!root.isAutotile) {
                root.activated(root.modelData.id)
            }
        }
    }

    Keys.onReturnPressed: {
        // Only allow editing manual layouts, not autotile algorithms
        if (!root.isAutotile) {
            root.activated(root.modelData.id)
        }
    }
    Keys.onDeletePressed: {
        if (!root.modelData.isSystem && !root.isAutotile) {
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
                    transformOrigin: Item.Center

                    // Safe scale calculation - fit thumbnail within parent bounds
                    readonly property real safeImplicitWidth: Math.max(1, implicitWidth)
                    readonly property real safeImplicitHeight: Math.max(1, implicitHeight)
                    readonly property real safeParentWidth: Math.max(1, parent.width)
                    readonly property real safeParentHeight: Math.max(1, parent.height)
                    scale: Math.min(1, safeParentWidth / safeImplicitWidth, safeParentHeight / safeImplicitHeight)
                }

                // Default layout indicator (top-left)
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

            // Info row with category badge
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                spacing: Kirigami.Units.smallSpacing

                QFZCommon.CategoryBadge {
                    visible: root.modelData.category !== undefined
                    category: root.modelData.category !== undefined ? root.modelData.category : 0
                }

                Label {
                    elide: Text.ElideRight
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    color: root.isSelected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor

                    text: {
                        var zoneCount = root.modelData.zoneCount || 0
                        if (root.isAutotile) {
                            return i18n("Dynamic")
                        } else if (root.modelData.isSystem) {
                            return i18n("System • %1", zoneCount)
                        } else {
                            return i18n("%1 zones", zoneCount)
                        }
                    }
                }
            }
        }
    }
}
