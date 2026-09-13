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
    implicitWidth: 654
    implicitHeight: (wide ? 222 : 456) + (scrolling ? 44 : 0)

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
        columnSpacing: 20
        rowSpacing: 12
        Rectangle {
            id: mapCanvas
            Layout.fillWidth: true
            Layout.preferredWidth: root.wide ? (root.width - 20) * 1.7 / 2.7 : root.width
            Layout.preferredHeight: 222
            Layout.minimumWidth: 0
            color: Appearance.recess
            radius: Appearance.radius * 0.6
            clip: true
            Repeater {
                model: !root.scrolling && root.map ? root.map.cells : []
                WindowMapCard {
                    id: card
                    required property var modelData
                    required property int index
                    colorIndex: modelData.colorIndex >= 0 ? modelData.colorIndex : index
                    windowInfo: modelData
                    x: 5 + modelData.x * (mapCanvas.width - 10)
                    y: 5 + modelData.y * (mapCanvas.height - 10)
                    width: Math.max(1, modelData.w * (mapCanvas.width - 10) - 4)
                    height: Math.max(1, modelData.h * (mapCanvas.height - 10) - 4)
                    selected: !!modelData.windowId && root.selectedId === modelData.windowId
                    draggable: !!modelData.windowId
                    onClicked: {
                        const index = root.windows.findIndex(w => w.windowId === modelData.windowId);
                        if (index >= 0)
                            root.choose(index);
                        else if (root.map && root.selectedWindow)
                            root.map.placeNavigationWindowInZone(root.selectedId, modelData.id);
                        else if (root.map)
                            root.map.activate(modelData.id);
                    }
                    onDragFinished: (localX, localY) => {
                        const point = card.mapToItem(mapCanvas, localX, localY);
                        const x = (point.x - 5) / (mapCanvas.width - 10), y = (point.y - 5) / (mapCanvas.height - 10);
                        const target = root.map.cells.find(c => x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h);
                        if (target && target.id !== modelData.id)
                            root.map.moveCell(modelData.id, target.id);
                    }
                }
            }
            ListView {
                id: strip
                objectName: "windowStrip"
                anchors.fill: parent
                anchors.margins: 5
                visible: root.scrolling
                orientation: ListView.Horizontal
                spacing: 8
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.scrolling ? root.windows : []
                ScrollBar.horizontal: ScrollBar {
                    policy: ScrollBar.AlwaysOn
                    height: 5
                }
                delegate: WindowMapCard {
                    required property var modelData
                    required property int index
                    width: 104
                    height: 196
                    colorIndex: modelData.colorIndex >= 0 ? modelData.colorIndex : index
                    windowInfo: modelData
                    selected: root.selectedId === modelData.windowId
                    onClicked: root.choose(index)
                }
                Rectangle {
                    parent: strip.contentItem
                    x: {
                        const index = root.windows.findIndex(w => !w.offscreen && !w.minimized);
                        return Math.max(0, index) * 112 - 2;
                    }
                    y: -2
                    width: Math.max(1, root.windows.filter(w => !w.offscreen && !w.minimized).length) * 112 - 4
                    height: 204
                    visible: root.windows.length > 0
                    radius: 8
                    color: "transparent"
                    border.width: 2
                    border.color: Qt.alpha(Appearance.text, 0.65)
                }
            }
        }
        ListView {
            id: titles
            objectName: "windowTitles"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: root.wide ? (root.width - 20) / 2.7 : root.width
            Layout.preferredHeight: 222
            clip: true
            spacing: 6
            boundsBehavior: Flickable.StopAtBounds
            model: root.windows
            ScrollBar.vertical: ScrollBar {
                active: titles.contentHeight > titles.height
            }
            delegate: WindowMapCard {
                required property var modelData
                required property int index
                width: titles.width
                height: 56
                row: true
                colorIndex: modelData.colorIndex >= 0 ? modelData.colorIndex : index
                windowInfo: modelData
                selected: root.selectedId === modelData.windowId
                onClicked: root.choose(index)
            }
            Text {
                anchors.centerIn: parent
                width: parent.width
                visible: !root.windows.length
                text: qsTr("No windows in this workspace")
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 11
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.columnSpan: root.wide ? 2 : 1
            Layout.preferredHeight: 32
            visible: root.scrolling
            ShellButton {
                text: qsTr("← Previous")
                labelSize: 10
                enabled: root.selectedIndex > 0
                onClicked: root.select(root.selectedIndex - 1)
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Window %1 of %2 · Drag scrollbar to browse").arg(Math.max(0, root.selectedIndex + 1)).arg(root.windows.length)
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 10
                elide: Text.ElideRight
            }
            ShellButton {
                text: qsTr("Next →")
                labelSize: 10
                enabled: root.selectedIndex >= 0 && root.selectedIndex < root.windows.length - 1
                onClicked: root.select(root.selectedIndex + 1)
            }
        }
    }
}
