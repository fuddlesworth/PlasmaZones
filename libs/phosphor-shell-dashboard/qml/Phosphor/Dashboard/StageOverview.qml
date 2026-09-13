// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property string screenName: ""
    property var workspaces: null
    property var mapFor: null
    property bool open: false
    readonly property bool fullScreen: true
    readonly property int desktopCount: workspaces ? workspaces.count : 0
    readonly property int currentDesktop: workspaces ? workspaces.activeIndex - 1 : 0
    readonly property var map: mapFor ? mapFor(currentDesktop) : null
    readonly property var windows: map ? (map.windows ?? []) : []
    property string selectedId: ""
    readonly property int selectedIndex: windows.findIndex(w => w.windowId === selectedId)
    readonly property var selectedWindow: selectedIndex >= 0 ? windows[selectedIndex] : null
    readonly property bool compact: width < 1100
    readonly property real sideWidth: compact ? 108 : 168
    readonly property real inspectorWidth: compact ? 212 : 268
    readonly property real inset: Appearance.gap + 8
    // Preserve the output's aspect so the compositor uses a uniform scale.
    readonly property real previewWidth: Math.max(100, width - sideWidth - inspectorWidth - 4 * inset)
    readonly property real previewHeight: previewWidth * height / Math.max(1, width)
    readonly property rect previewRect: Qt.rect(sideWidth + 2 * inset, Math.max(106, (height - previewHeight) / 2 - 20), previewWidth, previewHeight)
    signal closeRequested
    signal released
    property real progress: 0
    Behavior on progress {
        NumberAnimation {
            duration: Motion.reducedMotion ? 0 : 260
            easing.type: Easing.OutCubic
        }
    }
    visible: open || progress > 0
    enabled: open
    focus: true
    opacity: progress
    onOpenChanged: {
        progress = open ? 1 : 0;
        if (open) {
            refreshPreview();
            forceActiveFocus();
        } else {
            desktop.hide();
            release.restart();
        }
    }
    onPreviewRectChanged: if (open)
        Qt.callLater(refreshPreview)
    Component.onCompleted: {
        reconcile();
        if (open) {
            progress = 1;
            refreshPreview();
            forceActiveFocus();
        }
    }
    onWindowsChanged: reconcile()
    onMapChanged: {
        reconcile();
        if (map)
            map.refreshMenu();
    }
    Timer {
        id: release
        interval: Motion.reducedMotion ? 0 : 270
        onTriggered: root.released()
    }
    DesktopStage {
        id: desktop
    }
    function refreshPreview() {
        if (open && map && typeof map.refreshGeometry === "function")
            map.refreshGeometry();
        if (open && width > 0 && height > 0)
            desktop.show(screenName, Qt.rect(previewRect.x / width, previewRect.y / height, previewRect.width / width, previewRect.height / height), !Motion.reducedMotion);
    }
    function reconcile() {
        select(selectedIndex < 0 ? Math.max(0, windows.findIndex(w => w.focused)) : selectedIndex);
    }
    function select(index) {
        if (!windows.length) {
            selectedId = "";
            return;
        }
        const i = Math.max(0, Math.min(windows.length - 1, index));
        selectedId = windows[i].windowId;
        windowList.positionViewAtIndex(i, ListView.Contain);
    }
    function openSelected() {
        if (map && selectedWindow)
            map.activateNavigationWindow(selectedId);
        closeRequested();
    }
    function modeName(mode) {
        return mode === 0 ? qsTr("Snapping") : mode === 1 ? qsTr("Tiling") : mode === 2 ? qsTr("Scrolling") : qsTr("Placement off");
    }
    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_Escape:
            closeRequested();
            break;
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
            openSelected();
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    // Four bands leave a transparent aperture onto the live, scaled desktop.
    Repeater {
        model: [Qt.rect(0, 0, root.width, root.previewRect.y), Qt.rect(0, root.previewRect.y, root.previewRect.x, root.previewRect.height), Qt.rect(root.previewRect.x + root.previewRect.width, root.previewRect.y, root.width - root.previewRect.x - root.previewRect.width, root.previewRect.height), Qt.rect(0, root.previewRect.y + root.previewRect.height, root.width, root.height - root.previewRect.y - root.previewRect.height)]
        Rectangle {
            required property var modelData
            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.height
            color: Qt.alpha(Appearance.surface, 0.96)
            TapHandler {
                onTapped: root.closeRequested()
            }
        }
    }
    Column {
        x: root.previewRect.x
        y: Math.max(24, root.previewRect.y - 68)
        spacing: 8
        Text {
            text: qsTr("WORKSPACE %1 / %2").arg(String(root.currentDesktop + 1).padStart(2, "0")).arg(root.modeName(root.map ? root.map.mode : -1).toUpperCase())
            color: Appearance.muted
            font.pixelSize: 10
            font.letterSpacing: 1.2
        }
        Text {
            text: qsTr("Shape your space")
            color: Appearance.text
            font.family: Tokens.font_family_ui
            font.pixelSize: root.compact ? 23 : 30
            font.weight: Font.DemiBold
        }
    }
    ListView {
        x: root.inset
        y: 104
        width: root.sideWidth
        height: root.height - 190
        clip: true
        spacing: 12
        model: root.desktopCount
        ScrollBar.vertical: ScrollBar {}
        delegate: AbstractButton {
            id: workspace
            required property int index
            readonly property var map: root.mapFor ? root.mapFor(index) : null
            width: ListView.view.width
            height: width * 0.66 + 30
            padding: 10
            Accessible.name: qsTr("Workspace %1").arg(index + 1)
            onClicked: if (root.map)
                root.map.switchDesktop(index)
            background: Rectangle {
                radius: Math.min(14, Appearance.radius)
                color: Appearance.card
                border.width: 1
                border.color: workspace.index === root.currentDesktop || workspace.visualFocus ? Appearance.accent : Appearance.outline
            }
            contentItem: ColumnLayout {
                spacing: 10
                PlacementMiniature {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: workspace.map
                    interactive: false
                }
                Text {
                    text: qsTr("Workspace %1").arg(workspace.index + 1)
                    color: Appearance.text
                    font.pixelSize: 11
                }
            }
        }
    }
    Item {
        x: root.previewRect.x
        y: root.previewRect.y
        width: root.previewRect.width
        height: root.previewRect.height
        clip: true
        Rectangle {
            anchors.fill: parent
            color: desktop.active ? "transparent" : Appearance.surface
            border.width: 1
            border.color: Appearance.outline
        }
        PlacementMiniature {
            anchors.fill: parent
            anchors.margins: 12
            visible: !desktop.active
            model: root.map
            interactive: true
            labels: true
            onCellClicked: id => {
                const cell = root.map.cells.find(c => c.id === id);
                if (cell && cell.windowId)
                    root.selectedId = cell.windowId;
                else
                    root.map.activate(id);
            }
        }
        Repeater {
            model: desktop.active ? root.windows : []
            delegate: AbstractButton {
                required property var modelData
                required property int index
                readonly property var area: root.map.workArea
                // Placement cells use work-area fractions; Stage transforms
                // the whole output, including the reserved bar inset.
                x: (area.x + modelData.x * area.width) * root.previewRect.width / root.width
                y: (area.y + modelData.y * area.height) * root.previewRect.height / root.height
                width: modelData.w * area.width * root.previewRect.width / root.width
                height: modelData.h * area.height * root.previewRect.height / root.height
                visible: !modelData.offscreen && !modelData.minimized
                Accessible.name: modelData.title || modelData.appId
                onClicked: root.select(index)
                onDoubleClicked: root.openSelected()
                background: Rectangle {
                    color: "transparent"
                    border.width: root.selectedId === modelData.windowId ? 2 : 0
                    border.color: Appearance.at(modelData.t)
                }
            }
        }
    }
    ShellSurface {
        x: root.width - root.inspectorWidth - root.inset
        y: 104
        width: root.inspectorWidth
        height: root.height - 184
        railT: 0.82
        Flickable {
            anchors.fill: parent
            anchors.margins: 16
            contentWidth: width
            contentHeight: inspector.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: inspector
                width: parent.width
                spacing: 12
                Text {
                    text: qsTr("THIS WORKSPACE")
                    color: Appearance.muted
                    font.pixelSize: 10
                    font.letterSpacing: 1
                }
                Text {
                    text: qsTr("Placement")
                    color: Appearance.text
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
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
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Appearance.outline
                }
                Text {
                    text: qsTr("SELECTED WINDOW")
                    color: Appearance.muted
                    font.pixelSize: 10
                }
                Text {
                    Layout.fillWidth: true
                    text: root.selectedWindow ? root.selectedWindow.title || root.selectedWindow.appId : qsTr("No window selected")
                    color: Appearance.text
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                ShellButton {
                    Layout.fillWidth: true
                    text: qsTr("Open window")
                    enabled: !!root.selectedWindow
                    onClicked: root.openSelected()
                }
                ShellButton {
                    Layout.fillWidth: true
                    text: qsTr("Toggle floating")
                    enabled: !!root.selectedWindow
                    onClicked: root.map.toggleFloat(root.selectedWindow.id)
                }
                Text {
                    text: qsTr("MOVE TO WORKSPACE")
                    color: Appearance.muted
                    font.pixelSize: 10
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: root.desktopCount
                        ShellButton {
                            required property int index
                            text: String(index + 1).padStart(2, "0")
                            label: qsTr("Move to workspace %1").arg(index + 1)
                            enabled: !!root.selectedWindow && index !== root.currentDesktop
                            onClicked: root.map.moveToDesktop(root.selectedWindow.id, index)
                        }
                    }
                }
            }
        }
    }
    ListView {
        id: windowList
        onWidthChanged: Qt.callLater(root.reconcile)
        x: root.previewRect.x
        y: root.previewRect.y + root.previewRect.height + 16
        width: root.previewRect.width
        height: 64
        orientation: ListView.Horizontal
        spacing: 8
        clip: true
        model: root.windows
        ScrollBar.horizontal: ScrollBar {}
        delegate: ShellButton {
            required property var modelData
            required property int index
            width: 144
            height: 52
            label: modelData.title || modelData.appId
            highlighted: root.selectedId === modelData.windowId
            onClicked: root.select(index)
            onDoubleClicked: root.openSelected()
            contentItem: Text {
                text: (index + 1) + "  " + (modelData.title || modelData.appId)
                color: Appearance.text
                font.pixelSize: 11
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
    RowLayout {
        x: root.previewRect.x
        y: root.height - 56
        width: root.width - x - root.inset
        Text {
            Layout.fillWidth: true
            text: root.compact ? qsTr("Enter Open · Esc Return") : qsTr("← → Select window · Enter Open · Esc Return")
            color: Appearance.muted
            font.pixelSize: 11
        }
        ShellButton {
            text: qsTr("Return to desktop")
            onClicked: root.closeRequested()
        }
    }
}
