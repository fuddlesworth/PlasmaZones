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
    required property var appSettings
    required property real cellWidth
    required property real cellHeight
    property int viewMode: 0 // 0 = Snapping Layouts, 1 = Auto Tile
    // The full autotile default ID including prefix, for comparison
    readonly property string autotileDefaultId: "autotile:" + root.appSettings.defaultAutotileAlgorithm
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

    // HoverHandler for hover state — immune to scale transform geometry changes
    // that cause MouseArea.containsMouse to flicker at card boundaries
    HoverHandler {
        id: cardHoverHandler

        onHoveredChanged: root.isHovered = hovered
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: false
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
        anchors.margins: Kirigami.Units.smallSpacing
        radius: Kirigami.Units.smallSpacing * 1.5
        color: {
            if (root.isSelected)
                return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15);

            if (root.isHovered)
                return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06);

            return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03);
        }
        border.width: Math.round(Kirigami.Units.devicePixelRatio)
        border.color: {
            if (root.isSelected)
                return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5);

            if (root.isHovered)
                return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.3);

            return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08);
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

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
                    fontFamily: root.appSettings ? root.appSettings.labelFontFamily : ""
                    fontSizeScale: root.appSettings ? root.appSettings.labelFontSizeScale : 1
                    fontWeight: root.appSettings ? root.appSettings.labelFontWeight : Font.Bold
                    fontItalic: root.appSettings ? root.appSettings.labelFontItalic : false
                    fontUnderline: root.appSettings ? root.appSettings.labelFontUnderline : false
                    fontStrikeout: root.appSettings ? root.appSettings.labelFontStrikeout : false
                    transformOrigin: Item.Center
                    scale: Math.min(1, safeParentWidth / safeImplicitWidth, safeParentHeight / safeImplicitHeight)
                }

                // Top-left indicator row (default star + restriction badge)
                Row {
                    // Status icons use ToolTip.delay instead of HoverHandler to
                    // avoid "mouse grabber ambiguous" warnings.  HoverHandler on
                    // small items inside a parent MouseArea creates competing
                    // hover targets that Qt cannot resolve unambiguously.

                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing / 2

                    Kirigami.Icon {
                        id: defaultIcon

                        source: "favorite"
                        visible: root.viewMode === 1 ? root.modelData.id === root.autotileDefaultId : root.modelData.id === root.appSettings.defaultLayoutId
                        width: Kirigami.Units.iconSizes.small
                        height: Kirigami.Units.iconSizes.small
                        color: Kirigami.Theme.positiveTextColor
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.visible: defaultIconMA.containsMouse && visible
                        ToolTip.text: root.viewMode === 1 ? i18n("Default autotile algorithm") : i18n("Default layout")

                        MouseArea {
                            id: defaultIconMA

                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }

                    }

                    Kirigami.Icon {
                        source: root.modelData.isSystem ? "lock" : "document-edit"
                        visible: root.modelData.isSystem === true || root.modelData.hasSystemOrigin === true
                        width: Kirigami.Units.iconSizes.small
                        height: Kirigami.Units.iconSizes.small
                        color: Kirigami.Theme.disabledTextColor
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.visible: systemIconMA.containsMouse && visible
                        ToolTip.text: {
                            if (root.modelData.isAutotile && root.modelData.isSystem)
                                return i18n("Bundled algorithm");

                            if (root.modelData.isSystem)
                                return i18n("System layout (read-only)");

                            return i18n("Modified system layout");
                        }

                        MouseArea {
                            id: systemIconMA

                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
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
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.visible: filterIconMA.containsMouse && visible
                        ToolTip.text: i18n("This layout is restricted to specific screens, desktops, or activities")

                        MouseArea {
                            id: filterIconMA

                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
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
                        onClicked: settingsController.setLayoutAutoAssign(root.modelData.id, !(root.modelData.autoAssign === true))
                        Accessible.name: i18n("Auto-assign layout")
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
                        onClicked: settingsController.setLayoutHidden(root.modelData.id, !root.modelData.hiddenFromSelector)
                        Accessible.name: i18n("Toggle layout visibility")
                        ToolTip.visible: hovered
                        ToolTip.text: root.modelData.hiddenFromSelector ? i18n("Hidden from zone selector. Click to show.") : i18n("Visible in zone selector. Click to hide.")
                    }

                }

            }

            // Info row with category and aspect ratio badges
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Kirigami.Units.smallSpacing

                QFZCommon.CategoryBadge {
                    visible: root.modelData.category !== undefined
                    category: root.modelData.category !== undefined ? root.modelData.category : 0
                    autoAssign: root.modelData.autoAssign === true
                }

                // Memory indicator for algorithms that persist split state
                Kirigami.Icon {
                    visible: root.modelData.supportsMemory === true
                    source: "document-save-symbolic"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    color: Kirigami.Theme.positiveTextColor
                    opacity: 0.7
                    Accessible.name: i18n("Persistent algorithm")
                    ToolTip.delay: Kirigami.Units.toolTipDelay
                    ToolTip.visible: memoryIconMA.containsMouse && visible
                    ToolTip.text: i18n("Remembers split positions across window changes")

                    MouseArea {
                        id: memoryIconMA

                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }

                }

                QFZCommon.AspectRatioBadge {
                    aspectRatioClass: root.modelData.aspectRatioClass || "any"
                }

                Label {
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                    color: root.isSelected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                    text: i18n("%1 zones", root.modelData.zoneCount || 0)
                }

            }

        }

        Behavior on color {
            ColorAnimation {
                duration: Kirigami.Units.shortDuration
            }

        }

        Behavior on border.color {
            ColorAnimation {
                duration: Kirigami.Units.shortDuration
            }

        }

        transform: Scale {
            origin.x: cardBackground.width / 2
            origin.y: cardBackground.height / 2
            xScale: root.isHovered ? 1.02 : 1
            yScale: root.isHovered ? 1.02 : 1

            Behavior on xScale {
                NumberAnimation {
                    duration: Kirigami.Units.shortDuration
                    easing.type: Easing.OutCubic
                }

            }

            Behavior on yScale {
                NumberAnimation {
                    duration: Kirigami.Units.shortDuration
                    easing.type: Easing.OutCubic
                }

            }

        }

    }

}
