// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property var map: null
    property bool menuFocused: false
    signal closeRequested
    implicitWidth: 680
    implicitHeight: 410
    focus: true
    Component.onCompleted: {
        if (map)
            map.refreshMenu();
        navigator.forceActiveFocus();
    }
    Keys.onEscapePressed: root.closeRequested()
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
        anchors.fill: parent
        anchors.margins: Appearance.padding
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: qsTr("Workspace %1").arg(root.map ? root.map.currentDesktop + 1 : 1)
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 11
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: qsTr("Your windows, within reach.")
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
            }
            ShellButton {
                iconName: "view-list-details"
                label: qsTr("Placement settings")
                highlighted: root.menuFocused
                onClicked: {
                    root.menuFocused = !root.menuFocused;
                    if (root.map)
                        root.map.refreshMenu();
                }
            }
            ShellButton {
                iconName: "window-close"
                label: qsTr("Close navigator")
                onClicked: root.closeRequested()
            }
        }
        WorkspaceNavigator {
            id: navigator
            Layout.fillWidth: true
            Layout.fillHeight: true
            map: root.map
            visible: !root.menuFocused
            onActivated: root.closeRequested()
        }
        ColumnLayout {
            visible: root.menuFocused
            Layout.fillWidth: true
            Layout.fillHeight: true
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: [qsTr("Snapping"), qsTr("Tiling"), qsTr("Scrolling")]
                    ShellButton {
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true
                        text: modelData
                        highlighted: root.map && root.map.mode === index
                        onClicked: root.map.setPlacementMode(index)
                    }
                }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 5
                model: root.map ? root.map.menuModel : []
                ScrollBar.vertical: ScrollBar {}
                delegate: ShellButton {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.kind === "verb" ? root.verbLabel(modelData.id) : modelData.name || modelData.id
                    highlighted: !!modelData.current
                    onClicked: root.map.applyMenuChoice(modelData.kind, modelData.id)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                orientation: ListView.Horizontal
                clip: true
                spacing: 6
                model: root.map ? root.map.desktopCount : 0
                delegate: ShellButton {
                    required property int index
                    width: 36
                    height: 30
                    text: String(index + 1).padStart(2, "0")
                    label: qsTr("Workspace %1").arg(index + 1)
                    highlighted: root.map && index === root.map.currentDesktop
                    onClicked: root.map.switchDesktop(index)
                }
            }
            Text {
                text: root.modeLabel(root.map ? root.map.mode : -1)
                color: Appearance.muted
                font.pixelSize: 11
            }
            Text {
                visible: root.width >= 600
                text: qsTr("↑ ↓ Select · Enter Open")
                color: Appearance.muted
                font.pixelSize: 10
            }
        }
    }
}
