// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme

FocusScope {
    id: root
    property var map: null
    property bool selectOnly: false
    property string selectedId: ""
    readonly property var windows: map ? (map.windows ?? map.cells.filter(c => c.occupied && c.windowId)) : []
    readonly property int selectedIndex: windows.findIndex(w => w.windowId === selectedId)
    readonly property var selectedWindow: selectedIndex >= 0 ? windows[selectedIndex] : null
    readonly property bool scrolling: map && map.mode === 2
    readonly property bool wide: width >= 540
    signal activated(string windowId)
    implicitWidth: 620
    implicitHeight: wide ? 250 : 360

    function select(index) {
        if (!windows.length) {
            selectedId = "";
            return;
        }
        const i = Math.max(0, Math.min(windows.length - 1, index));
        selectedId = windows[i].windowId;
        titles.positionViewAtIndex(i, ListView.Contain);
        strip.positionViewAtIndex(i, ListView.Contain);
    }
    function openSelection() {
        if (!selectedWindow || !map)
            return;
        if (typeof map.activateNavigationWindow === "function")
            map.activateNavigationWindow(selectedId);
        else
            map.activate(selectedWindow.id);
        activated(selectedId);
    }
    function choose(index) {
        select(index);
        if (!selectOnly)
            openSelection();
    }
    function reconcile() {
        if (selectedIndex < 0)
            select(Math.max(0, windows.findIndex(w => w.focused)));
        else
            select(selectedIndex);
    }
    onWindowsChanged: Qt.callLater(reconcile)
    Component.onCompleted: reconcile()
    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_Left:
        case Qt.Key_Up:
            select(selectedIndex - 1);
            break;
        case Qt.Key_Right:
        case Qt.Key_Down:
            select(selectedIndex + 1);
            break;
        case Qt.Key_Home:
            select(0);
            break;
        case Qt.Key_End:
            select(windows.length - 1);
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            openSelection();
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    GridLayout {
        anchors.fill: parent
        columns: root.wide ? 2 : 1
        columnSpacing: 18
        rowSpacing: 12
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: root.wide ? root.width * 0.52 : root.width
            spacing: 8
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: root.wide ? 155 : 108
                radius: Math.min(12, Appearance.radius)
                color: Qt.alpha(Appearance.text, 0.04)
                border.width: 1
                border.color: Appearance.outline
                PlacementMiniature {
                    anchors.fill: parent
                    anchors.margins: 12
                    model: root.map
                    interactive: true
                    labels: !root.scrolling
                    onCellClicked: id => {
                        const cell = root.map.cells.find(c => c.id === id);
                        const index = cell ? root.windows.findIndex(w => w.windowId === cell.windowId) : -1;
                        if (index >= 0)
                            root.choose(index);
                        else if (root.map)
                            root.map.activate(id);
                    }
                    onCellMoved: (fromId, toId) => root.map.moveCell(fromId, toId)
                    onCellFloatToggled: id => root.map.toggleFloat(id)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: root.scrolling
                ShellButton {
                    iconName: "go-previous"
                    label: qsTr("Previous window")
                    enabled: root.selectedIndex > 0
                    onClicked: root.select(root.selectedIndex - 1)
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("%1 of %2 windows").arg(root.selectedIndex + 1).arg(root.windows.length)
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 11
                }
                ShellButton {
                    iconName: "go-next"
                    label: qsTr("Next window")
                    enabled: root.selectedIndex >= 0 && root.selectedIndex < root.windows.length - 1
                    onClicked: root.select(root.selectedIndex + 1)
                }
            }
            ListView {
                id: strip
                objectName: "windowStrip"
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                visible: root.scrolling
                orientation: ListView.Horizontal
                spacing: 7
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.windows
                ScrollBar.horizontal: ScrollBar {}
                delegate: AbstractButton {
                    required property var modelData
                    required property int index
                    width: 112
                    height: 58
                    Accessible.name: modelData.title || modelData.appId || qsTr("Window %1").arg(index + 1)
                    onClicked: root.choose(index)
                    background: Rectangle {
                        radius: Math.min(10, Appearance.radius)
                        color: Qt.alpha(Appearance.at(modelData.t), root.selectedId === modelData.windowId ? 0.27 : 0.10)
                        border.width: 1
                        border.color: root.selectedId === modelData.windowId ? Appearance.at(modelData.t) : Appearance.outline
                    }
                    contentItem: Column {
                        spacing: 4
                        Text {
                            width: parent.width
                            text: String(index + 1).padStart(2, "0") + (modelData.offscreen ? "  ·  " + qsTr("Offscreen") : "")
                            color: Appearance.muted
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: modelData.appId || modelData.title || qsTr("Window")
                            color: Appearance.text
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                    padding: 8
                }
            }
        }
        ListView {
            id: titles
            objectName: "windowTitles"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: root.width * 0.44
            Layout.minimumHeight: 120
            Layout.maximumHeight: 250
            clip: true
            spacing: 5
            boundsBehavior: Flickable.StopAtBounds
            model: root.windows
            ScrollBar.vertical: ScrollBar {
                active: titles.contentHeight > titles.height
            }
            delegate: AbstractButton {
                id: row
                required property var modelData
                required property int index
                width: titles.width
                height: 56
                padding: 10
                Accessible.name: modelData.title || modelData.appId || qsTr("Window %1").arg(index + 1)
                onClicked: root.choose(index)
                background: Rectangle {
                    radius: Math.min(11, Appearance.radius)
                    color: root.selectedId === row.modelData.windowId ? Qt.alpha(Appearance.at(row.modelData.t), 0.18) : row.hovered ? Appearance.card : "transparent"
                    border.width: root.selectedId === row.modelData.windowId || row.visualFocus ? 1 : 0
                    border.color: row.visualFocus ? Appearance.text : Qt.alpha(Appearance.at(row.modelData.t), 0.55)
                }
                contentItem: RowLayout {
                    spacing: 10
                    Kirigami.Icon {
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                        source: row.modelData.appId || "application-x-executable"
                        fallback: "application-x-executable"
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.appId || qsTr("Window %1").arg(row.index + 1)
                            color: Appearance.text
                            font.family: Tokens.font_family_ui
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.title || (row.modelData.minimized ? qsTr("Minimized") : qsTr("Untitled window"))
                            color: Appearance.muted
                            font.family: Tokens.font_family_ui
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }
                    Text {
                        text: row.modelData.urgent ? "●" : row.modelData.offscreen ? "↗" : ""
                        color: Appearance.at(row.modelData.t)
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                width: parent.width
                visible: !root.windows.length
                text: qsTr("No windows in this workspace")
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: Appearance.muted
                font.pixelSize: 12
            }
        }
    }
}
