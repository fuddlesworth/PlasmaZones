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
    property var workspaces: null
    property var mapFor: null
    property bool menuFocused: false
    signal closeRequested
    implicitWidth: 700
    implicitHeight: layout.implicitHeight + (Appearance.padding + 1) * 2
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

    Flickable {
        anchors.fill: parent
        anchors.margins: Appearance.padding + 1
        anchors.bottomMargin: Appearance.padding - 2
        contentWidth: width
        contentHeight: layout.implicitHeight + 3
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
        ColumnLayout {
            id: layout
            width: parent.width
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 53
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: qsTr("WORKSPACE %1 / %2").arg(String(root.map ? root.map.currentDesktop + 1 : 1).padStart(2, "0")).arg(root.workspaces ? root.workspaces.activeName.toUpperCase() : "")
                        color: Appearance.muted
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 9
                        font.letterSpacing: 1.3
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: qsTr("Your windows, within reach.")
                        color: Appearance.text
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 18
                        font.weight: Font.Medium
                    }
                }
                Text {
                    visible: root.width > 620
                    text: qsTr("Placement")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 10
                }
                ShellComboBox {
                    implicitWidth: 91
                    implicitHeight: 33
                    model: [qsTr("Snapping"), qsTr("Tiling"), qsTr("Scrolling")]
                    currentIndex: root.map ? root.map.mode : -1
                    displayText: currentIndex < 0 ? qsTr("Off") : currentText
                    Accessible.name: qsTr("Placement")
                    onActivated: root.map.setPlacementMode(currentIndex)
                }
                Item {
                    Layout.preferredWidth: root.width > 620 ? 110 : 0
                }
                ShellButton {
                    text: root.menuFocused ? "‹" : "×"
                    label: root.menuFocused ? qsTr("Back to windows") : qsTr("Close navigator")
                    implicitWidth: 30
                    outlined: true
                    flat: true
                    labelSize: 17
                    onClicked: if (root.menuFocused)
                        root.menuFocused = false
                    else
                        root.closeRequested()
                }
            }
            WorkspaceNavigator {
                id: navigator
                Layout.fillWidth: true
                Layout.topMargin: 19
                Layout.preferredHeight: implicitHeight
                map: root.map
                visible: !root.menuFocused
                onActivated: root.closeRequested()
            }
            ListView {
                visible: root.menuFocused
                Layout.fillWidth: true
                Layout.topMargin: 19
                Layout.preferredHeight: 222
                clip: true
                spacing: 5
                model: root.map ? root.map.menuModel : []
                ScrollBar.vertical: ScrollBar {}
                delegate: ShellButton {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.kind === "verb" ? root.verbLabel(modelData.id) : modelData.name || modelData.id
                    highlighted: !!modelData.current
                    onClicked: {
                        root.map.applyMenuChoice(modelData.kind, modelData.id);
                        root.menuFocused = false;
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 20
                height: 1
                color: Appearance.outline
            }
            ListView {
                id: desktopList
                Layout.fillWidth: true
                Layout.topMargin: 15
                Layout.preferredHeight: 34
                orientation: ListView.Horizontal
                clip: true
                spacing: 8
                model: root.workspaces ? root.workspaces.model : null
                delegate: AbstractButton {
                    id: desktopChoice
                    required property int index
                    required property string name
                    required property string workspaceId
                    required property bool isActive
                    readonly property var map: root.mapFor ? root.mapFor(index) : null
                    width: Math.max(110, (desktopList.width - 8 * Math.max(0, desktopList.count - 1)) / Math.max(1, desktopList.count))
                    height: 34
                    padding: 11
                    Accessible.name: name
                    onClicked: root.workspaces.switchTo(workspaceId)
                    background: Rectangle {
                        radius: 8
                        color: desktopChoice.isActive ? Appearance.card : "transparent"
                        border.width: 1
                        border.color: desktopChoice.isActive ? Appearance.accent : Appearance.outline
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        Text {
                            text: String(desktopChoice.index + 1).padStart(2, "0")
                            font.family: Tokens.font_family_mono
                            font.pixelSize: 10
                            color: Appearance.muted
                        }
                        Text {
                            Layout.fillWidth: true
                            text: desktopChoice.name
                            font.family: Tokens.font_family_ui
                            font.pixelSize: 10
                            color: desktopChoice.isActive ? Appearance.text : Appearance.muted
                            elide: Text.ElideRight
                        }
                        Text {
                            text: desktopChoice.map ? desktopChoice.map.windows.length : ""
                            font.family: Tokens.font_family_ui
                            font.pixelSize: 10
                            color: Appearance.muted
                        }
                    }
                }
            }
            Item {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.preferredHeight: 14
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Keycap {
                        text: "←"
                    }
                    Keycap {
                        text: "→"
                    }
                    Text {
                        text: qsTr("Select")
                        color: Appearance.muted
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Keycap {
                        text: "Enter"
                    }
                    Text {
                        text: qsTr("Focus")
                        color: Appearance.muted
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Keycap {
                        text: "Esc"
                    }
                    Text {
                        text: qsTr("Close")
                        color: Appearance.muted
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.width >= 620
                    text: qsTr("Click a window to focus it")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 10
                }
            }
        }
    }
}
