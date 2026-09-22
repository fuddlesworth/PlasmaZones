// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.MapPane, the expanded placement map (A2 §1.1, §1.5).
//
// The content of the bar's pane when the map chip is long-pressed or
// right-clicked: the same miniature at 96 px height with labels on, the
// desktop ticks grown into clickable desktop buttons, and the map's
// context menu as a column of plain text rows with a 2 px underline
// under the current choice, driven by the model's `menuModel`. No QQC2
// Menu: the bar takes no keyboard focus, so the menu is pointer-only
// and lives in the pane rather than in a popup of its own.
//
// Every row routes through the model: a desktop button switches, a
// choice row applies the layout / algorithm / template to this screen
// and desktop, a verb row runs the verb. Labels for verbs are the
// pane's, since the model hands over ids only.

import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    // A PlacementMapScreen.
    property var map: null
    // Open on the menu section: the map is drawn smaller so the rows
    // are in reach without scrolling.
    property bool menuFocused: false

    signal closeRequested

    readonly property int mapHeight: root.menuFocused ? 64 : 96
    readonly property real _aspect: root.map && root.map.aspect > 0 ? root.map.aspect : 16 / 9
    readonly property int _mapW: Math.round(Math.min(320, root.mapHeight * root._aspect))

    implicitWidth: column.implicitWidth + 2 * Tokens.spacing_l
    implicitHeight: column.implicitHeight + 2 * Tokens.spacing_l

    onVisibleChanged: {
        if (visible && root.map)
            root.map.refreshMenu();
    }
    Component.onCompleted: {
        if (root.map)
            root.map.refreshMenu();
    }

    function verbLabel(id) {
        switch (id) {
        case "editLayout":
            return qsTr("Edit layout");
        case "snapAll":
            return qsTr("Snap all windows");
        case "retile":
            return qsTr("Retile");
        case "promoteToMaster":
            return qsTr("Promote to master");
        case "toggleMaximizeColumn":
            return qsTr("Maximize column");
        default:
            return id;
        }
    }

    function modeLabel(mode) {
        switch (mode) {
        case 0:
            return qsTr("Snapping");
        case 1:
            return qsTr("Tiling");
        case 2:
            return qsTr("Scrolling");
        default:
            return qsTr("Placement off");
        }
    }

    ColumnLayout {
        id: column

        anchors.fill: parent
        anchors.margins: Tokens.spacing_l
        spacing: Tokens.spacing_m

        // Header: the mode, and a close row on the right.
        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.spacing_m

            Text {
                Layout.fillWidth: true
                text: root.modeLabel(root.map ? root.map.mode : -1)
                color: Theme.on_surface
                opacity: 0.9
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_label_l
                font.weight: Tokens.font_weight_medium
                elide: Text.ElideRight
            }
            Text {
                text: qsTr("Close")
                color: Theme.on_surface
                opacity: closeHover.hovered ? 1 : 0.6
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_label_m

                Accessible.role: Accessible.Button
                Accessible.name: text

                HoverHandler {
                    id: closeHover

                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: root.closeRequested()
                }
            }
        }

        // The map, large, with labels.
        PlacementMiniature {
            id: mini

            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: root._mapW
            Layout.preferredHeight: root.mapHeight
            model: root.map
            interactive: true
            labels: true
            onCellClicked: id => {
                if (root.map)
                    root.map.activate(id);
            }
            onCellMoved: (fromId, toId) => {
                if (root.map)
                    root.map.moveCell(fromId, toId);
            }
            onCellFloatToggled: id => {
                if (root.map)
                    root.map.toggleFloat(id);
            }
        }

        // Desktop buttons: the ticks, grown to be clicked. The current
        // desktop is white at 90 % with a 2 px underline.
        Row {
            Layout.alignment: Qt.AlignHCenter
            spacing: Tokens.spacing_s

            Repeater {
                model: root.map ? root.map.desktopCount : 0
                delegate: Item {
                    id: desktopButton

                    required property int index
                    readonly property bool current: root.map && index === root.map.currentDesktop

                    width: Math.max(16, number.contentWidth + Tokens.spacing_xs)
                    height: number.contentHeight + Tokens.spacing_xs

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Desktop %1").arg(index + 1)

                    TabularText {
                        id: number

                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        text: String(desktopButton.index + 1)
                        font.pixelSize: Tokens.font_size_label_m
                        opacity: desktopButton.current ? 0.9 : desktopHover.hovered ? 0.7 : 0.4
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: Spectrum.focus
                        opacity: desktopButton.current ? 0.9 : 0
                        Behavior on opacity {
                            NumberAnimation {
                                duration: desktopButton.current ? Motion.duration_enter : Motion.duration_release
                                easing: desktopButton.current ? Motion.enter : Motion.release
                            }
                        }
                    }
                    HoverHandler {
                        id: desktopHover

                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: {
                            if (root.map)
                                root.map.switchDesktop(desktopButton.index);
                        }
                    }
                }
            }
        }

        // Hairline between the map and the menu.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.on_surface
            opacity: 0.25
            visible: menu.count > 0
        }

        // The menu: choices first, then verbs, as the model lists them.
        Column {
            id: menuColumn

            Layout.fillWidth: true
            spacing: 0

            Repeater {
                id: menu

                model: root.map ? root.map.menuModel : []
                delegate: Item {
                    id: row

                    required property int index
                    required property var modelData
                    readonly property bool isVerb: modelData.kind === "verb"
                    readonly property bool current: !isVerb && !!modelData.current
                    readonly property string label: isVerb ? root.verbLabel(String(modelData.id)) : (modelData.name !== undefined && String(modelData.name) !== "" ? String(modelData.name) : String(modelData.id))

                    width: menuColumn.width
                    height: rowText.contentHeight + Tokens.spacing_s

                    Accessible.role: Accessible.MenuItem
                    Accessible.name: label

                    Text {
                        id: rowText

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.label
                        color: Theme.on_surface
                        opacity: row.current || rowHover.hovered ? 1 : 0.7
                        font.family: Tokens.font_family_ui
                        font.pixelSize: Tokens.font_size_label_m
                        font.weight: row.isVerb ? Tokens.font_weight_medium : Tokens.font_weight_regular
                        elide: Text.ElideRight
                    }
                    // 2 px underline under the current choice, in the
                    // rail's hue at this pane's x; hover shows it faintly.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        color: Spectrum.focus
                        opacity: row.current ? 0.9 : rowHover.hovered ? 0.35 : 0
                        Behavior on opacity {
                            NumberAnimation {
                                duration: row.current ? Motion.duration_enter : Motion.duration_release
                                easing: row.current ? Motion.enter : Motion.release
                            }
                        }
                    }
                    HoverHandler {
                        id: rowHover

                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: {
                            if (root.map)
                                root.map.applyMenuChoice(String(row.modelData.kind), String(row.modelData.id));
                        }
                    }
                }
            }
        }
    }
}
