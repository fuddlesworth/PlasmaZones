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
                    fontFamily: root.kcm ? root.kcm.labelFontFamily : ""
                    fontSizeScale: root.kcm ? root.kcm.labelFontSizeScale : 1.0
                    fontWeight: root.kcm ? root.kcm.labelFontWeight : Font.Bold
                    fontItalic: root.kcm ? root.kcm.labelFontItalic : false
                    fontUnderline: root.kcm ? root.kcm.labelFontUnderline : false
                    fontStrikeout: root.kcm ? root.kcm.labelFontStrikeout : false
                    transformOrigin: Item.Center

                    // Safe scale calculation - fit thumbnail within parent bounds
                    readonly property real safeImplicitWidth: Math.max(1, implicitWidth)
                    readonly property real safeImplicitHeight: Math.max(1, implicitHeight)
                    readonly property real safeParentWidth: Math.max(1, parent.width)
                    readonly property real safeParentHeight: Math.max(1, parent.height)
                    scale: Math.min(1, safeParentWidth / safeImplicitWidth, safeParentHeight / safeImplicitHeight)
                }

                // Top-left indicator row (default star + restriction badge)
                Row {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing / 2

                    Kirigami.Icon {
                        id: defaultIcon
                        source: "favorite"
                        visible: root.modelData.id === root.kcm.defaultLayoutId
                        width: Kirigami.Units.iconSizes.small
                        height: Kirigami.Units.iconSizes.small
                        color: Kirigami.Theme.positiveTextColor

                        HoverHandler { id: defaultIconHover }
                        ToolTip.visible: defaultIconHover.hovered
                        ToolTip.text: i18n("Default layout")
                    }

                    Kirigami.Icon {
                        source: "view-filter"
                        visible: {
                            var d = root.modelData
                            var s = d.allowedScreens
                            var k = d.allowedDesktops
                            var a = d.allowedActivities
                            return (s !== undefined && s !== null && s.length > 0) ||
                                   (k !== undefined && k !== null && k.length > 0) ||
                                   (a !== undefined && a !== null && a.length > 0)
                        }
                        width: Kirigami.Units.iconSizes.small
                        height: Kirigami.Units.iconSizes.small
                        color: Kirigami.Theme.disabledTextColor

                        HoverHandler { id: filterIconHover }
                        ToolTip.visible: filterIconHover.hovered
                        ToolTip.text: i18n("This layout has per-screen/desktop/activity restrictions")
                    }
                }

                // Top-right toggle buttons (autoAssign and hidden are independent:
                // a layout can be hidden from the zone selector while still auto-assigning
                // new windows when active via screen/desktop/activity assignment)
                Row {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: Kirigami.Units.smallSpacing / 2
                    spacing: 0

                    // Auto-assign toggle
                    ToolButton {
                        width: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                        height: width
                        padding: 0
                        visible: root.isHovered || root.modelData.autoAssign === true
                        icon.name: root.modelData.autoAssign === true ? "window-duplicate" : "window-new"
                        icon.width: Kirigami.Units.iconSizes.small
                        icon.height: Kirigami.Units.iconSizes.small
                        icon.color: root.modelData.autoAssign === true ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                        onClicked: root.kcm.setLayoutAutoAssign(root.modelData.id, !(root.modelData.autoAssign === true))

                        ToolTip.visible: hovered
                        ToolTip.text: root.modelData.autoAssign === true
                            ? i18n("Auto-assign enabled: new windows fill empty zones. Click to disable.")
                            : i18n("Click to auto-assign new windows to empty zones")
                    }

                    // Visibility toggle
                    ToolButton {
                        width: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                        height: width
                        padding: 0
                        visible: root.isHovered || root.modelData.hiddenFromSelector === true
                        icon.name: root.modelData.hiddenFromSelector ? "view-hidden" : "view-visible"
                        icon.width: Kirigami.Units.iconSizes.small
                        icon.height: Kirigami.Units.iconSizes.small
                        icon.color: root.modelData.hiddenFromSelector ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.textColor
                        onClicked: root.kcm.setLayoutHidden(root.modelData.id, !root.modelData.hiddenFromSelector)

                        ToolTip.visible: hovered
                        ToolTip.text: root.modelData.hiddenFromSelector
                            ? i18n("Hidden from zone selector. Click to show.")
                            : i18n("Visible in zone selector. Click to hide.")
                    }
                }

                // Dim thumbnail when hidden
                opacity: root.modelData.hiddenFromSelector ? 0.5 : 1.0

            }

            // Info row with category badge
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                spacing: Kirigami.Units.smallSpacing

                QFZCommon.CategoryBadge {
                    visible: root.modelData.category !== undefined
                    category: root.modelData.category !== undefined ? root.modelData.category : 0
                    autoAssign: root.modelData.autoAssign === true
                }

                Label {
                    Layout.fillWidth: true
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
}
