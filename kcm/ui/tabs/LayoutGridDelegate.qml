// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

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
    property int viewMode: 0 // 0 = Snapping Layouts, 1 = Auto Tile
    // The full autotile default ID including prefix, for comparison
    readonly property string autotileDefaultId: "autotile:" + root.kcm.autotileAlgorithm
    // Selection state (bound from parent GridView)
    property bool isSelected: false
    property bool isHovered: false

    // Signals
    signal selected(int index)
    signal activated(string layoutId)
    signal deleteRequested(var layout)
    signal exportRequested(string layoutId)
    signal setAsDefaultRequested(var layout)
    signal contextMenuRequested(var layout)

    width: cellWidth
    height: cellHeight
    Accessible.name: modelData.name || i18n("Unnamed Layout")
    Accessible.description: i18n("Layout with %1 zones", modelData.zoneCount || 0)
    Accessible.role: Accessible.ListItem
    Keys.onReturnPressed: root.activated(root.modelData.id)
    Keys.onDeletePressed: {
        if (!root.modelData.isSystem && !root.modelData.isAutotile)
            root.deleteRequested(root.modelData);

    }

    HoverHandler {
        id: cardHover
        // HoverHandler doesn't get stolen by child ToolButtons, unlike
        // MouseArea.containsMouse which flickers when children intercept hover.
        onHoveredChanged: root.isHovered = hovered
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                root.selected(root.index);
                root.contextMenuRequested(root.modelData);
            } else {
                root.selected(root.index);
            }
        }
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton)
                root.activated(root.modelData.id);

        }
    }

    Rectangle {
        id: cardBackground

        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing / 2
        radius: Kirigami.Units.smallSpacing
        color: root.isSelected ? Kirigami.Theme.highlightColor : root.isHovered ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2) : "transparent"
        border.color: root.isSelected ? Kirigami.Theme.highlightColor : root.isHovered ? Kirigami.Theme.disabledTextColor : "transparent"
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing / 2

            // Thumbnail area
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Dim thumbnail when hidden
                opacity: root.modelData.hiddenFromSelector ? 0.5 : 1

                LayoutThumbnail {
                    id: layoutThumbnail

                    // Safe scale calculation - fit thumbnail within parent bounds
                    readonly property real safeImplicitWidth: Math.max(1, implicitWidth)
                    readonly property real safeImplicitHeight: Math.max(1, implicitHeight)
                    readonly property real safeParentWidth: Math.max(1, parent.width)
                    readonly property real safeParentHeight: Math.max(1, parent.height)

                    anchors.centerIn: parent
                    layout: root.modelData
                    isSelected: root.isSelected
                    fontFamily: root.kcm ? root.kcm.labelFontFamily : ""
                    fontSizeScale: root.kcm ? root.kcm.labelFontSizeScale : 1
                    fontWeight: root.kcm ? root.kcm.labelFontWeight : Font.Bold
                    fontItalic: root.kcm ? root.kcm.labelFontItalic : false
                    fontUnderline: root.kcm ? root.kcm.labelFontUnderline : false
                    fontStrikeout: root.kcm ? root.kcm.labelFontStrikeout : false
                    transformOrigin: Item.Center
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
                        visible: root.viewMode === 1 ? root.modelData.id === root.autotileDefaultId : root.modelData.id === root.kcm.defaultLayoutId
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
                        source: root.modelData.isSystem ? "lock" : "document-edit"
                        visible: root.modelData.isSystem === true || root.modelData.hasSystemOrigin === true
                        width: Kirigami.Units.iconSizes.small
                        height: Kirigami.Units.iconSizes.small
                        color: Kirigami.Theme.disabledTextColor
                        ToolTip.visible: systemIconHover.hovered
                        ToolTip.text: root.modelData.isSystem ? i18n("System layout (read-only)") : i18n("Modified system layout")

                        HoverHandler {
                            id: systemIconHover
                        }

                    }

                    Kirigami.Icon {
                        source: "view-filter"
                        visible: {
                            var d = root.modelData;
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

                // Top-right toggle buttons (autoAssign and hidden are independent:
                // a layout can be hidden from the zone selector while still auto-assigning
                // new windows when active via screen/desktop/activity assignment)
                Row {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: Kirigami.Units.smallSpacing / 2
                    spacing: 0

                    // Auto-assign toggle (hidden for autotile — the tiling engine manages those)
                    ToolButton {
                        width: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                        height: width
                        padding: 0
                        visible: root.modelData.isAutotile !== true && (root.isHovered || root.modelData.autoAssign === true)
                        icon.name: root.modelData.autoAssign === true ? "window-duplicate" : "window-new"
                        icon.width: Kirigami.Units.iconSizes.small
                        icon.height: Kirigami.Units.iconSizes.small
                        icon.color: root.modelData.autoAssign === true ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                        onClicked: root.kcm.setLayoutAutoAssign(root.modelData.id, !(root.modelData.autoAssign === true))
                        ToolTip.visible: hovered
                        ToolTip.text: root.modelData.autoAssign === true ? i18n("Auto-assign enabled: new windows fill empty zones. Click to disable.") : i18n("Click to auto-assign new windows to empty zones")
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
                        ToolTip.text: root.modelData.hiddenFromSelector ? i18n("Hidden from zone selector. Click to show.") : i18n("Visible in zone selector. Click to hide.")
                    }

                }

            }

            // Info row with category badge
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Kirigami.Units.smallSpacing

                QFZCommon.CategoryBadge {
                    visible: root.modelData.category !== undefined
                    category: root.modelData.category !== undefined ? root.modelData.category : 0
                    autoAssign: root.modelData.autoAssign === true
                }

                Label {
                    elide: Text.ElideRight
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    color: root.isSelected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                    text: i18n("%1 zones", root.modelData.zoneCount || 0)
                }

            }

        }

    }

}
