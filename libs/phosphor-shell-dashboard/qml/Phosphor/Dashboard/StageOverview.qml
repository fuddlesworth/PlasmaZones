// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property string screenName: ""
    property var workspaces: null
    property var mapFor: null
    property var surfaceEffects: null
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
    readonly property real sideWidth: compact ? 112 : 170
    readonly property real inspectorWidth: compact ? 180 : 210
    readonly property real inset: compact ? 18 : 30
    readonly property real previewLeft: compact ? 150 : 228
    readonly property real previewTop: Math.min(174, height * 0.22)
    readonly property rect previewRect: Qt.rect(previewLeft, previewTop, Math.max(100, width - previewLeft - inspectorWidth - (compact ? 40 : 54)), Math.max(120, height - previewTop - Math.min(178, height * 0.2)))
    readonly property rect workArea: map && map.workArea && map.workArea.width > 0 && map.workArea.height > 0 ? map.workArea : Qt.rect(0, 0, width, height)
    // Fit the desktop canvas inside Stage without doubling its outer margin.
    readonly property real canvasLeft: Appearance.settings.desktopStyle ? 36 : 0
    readonly property real canvasTop: Appearance.settings.desktopStyle ? (Appearance.bottom ? 36 : 14) : 0
    readonly property real canvasBottom: Appearance.settings.desktopStyle ? (Appearance.bottom ? 14 : 54) : 0
    readonly property rect canvas: Qt.rect(workArea.x + canvasLeft, workArea.y + canvasTop, Math.max(1, workArea.width - 2 * canvasLeft), Math.max(1, workArea.height - canvasTop - canvasBottom))
    readonly property rect nativeRect: Qt.rect(previewRect.x - canvas.x * previewRect.width / canvas.width, previewRect.y - canvas.y * previewRect.height / canvas.height, width * previewRect.width / canvas.width, height * previewRect.height / canvas.height)
    readonly property rect barRect: Qt.rect(Appearance.barInset, Appearance.bottom ? height - Appearance.barOffset - Appearance.barHeight : Appearance.barOffset, width - Appearance.barInset * 2, Appearance.barHeight)
    readonly property bool scrolling: map && map.mode === 2
    readonly property string selectedApp: selectedWindow ? appName(selectedWindow.appId) : qsTr("No window selected")
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
    onNativeRectChanged: if (open)
        Qt.callLater(refreshPreview)
    onPreviewRectChanged: Qt.callLater(refreshSurface)
    onBarRectChanged: Qt.callLater(refreshSurface)
    onSurfaceEffectsChanged: Qt.callLater(refreshSurface)
    Window.onWindowChanged: Qt.callLater(refreshSurface)
    Component.onCompleted: {
        reconcile();
        refreshSurface();
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
            desktop.show(screenName, Qt.rect(nativeRect.x / width, nativeRect.y / height, nativeRect.width / width, nativeRect.height / height), !Motion.reducedMotion);
    }
    function refreshSurface() {
        if (surfaceEffects)
            surfaceEffects.setOverviewRegions(root, barRect, previewRect);
    }
    function appName(id) {
        const name = String(id || "").replace(/\.desktop$/, "").split(".").pop();
        return name ? name.charAt(0).toUpperCase() + name.slice(1).replace(/[-_]/g, " ") : qsTr("Window");
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

    // An aperture preserves the live desktop and the bar. The matching
    // input region lets clicks reach the bar's own layer surface.
    Shape {
        anchors.fill: parent
        ShapePath {
            strokeWidth: 0
            fillColor: Qt.alpha(Appearance.recess, 0.76)
            fillRule: ShapePath.OddEvenFill
            PathSvg {
                path: {
                    function rect(r) {
                        return "M " + r.x + " " + r.y + " h " + r.width + " v " + r.height + " h " + (-r.width) + " Z ";
                    }
                    return rect(Qt.rect(0, 0, root.width, root.height)) + rect(root.previewRect) + rect(root.barRect);
                }
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: root.closeRequested()
    }
    Column {
        x: root.previewRect.x + 2
        y: root.previewRect.y - 72
        spacing: 8
        Text {
            text: qsTr("WORKSPACE %1 / %2").arg(String(root.currentDesktop + 1).padStart(2, "0")).arg(root.modeName(root.map ? root.map.mode : -1).toUpperCase())
            color: Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: 9
            font.letterSpacing: 2
            font.weight: Font.DemiBold
        }
        Text {
            text: root.workspaces ? root.workspaces.activeName : qsTr("Workspace %1").arg(root.currentDesktop + 1)
            color: Appearance.text
            font.family: Tokens.font_family_ui
            font.pixelSize: 28
            font.weight: Font.Medium
        }
    }
    Text {
        x: root.previewRect.x
        y: root.previewRect.y - 50
        width: root.previewRect.width - 4
        horizontalAlignment: Text.AlignRight
        visible: !root.compact
        text: qsTr("Click to select · Enter or double-click to open")
        color: Appearance.muted
        font.family: Tokens.font_family_ui
        font.pixelSize: 11
    }
    ListView {
        id: workspaceList
        x: root.inset
        y: root.previewRect.y
        width: root.sideWidth
        height: root.height - y - 80
        clip: true
        spacing: 14
        model: root.workspaces ? root.workspaces.model : null
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        delegate: AbstractButton {
            id: workspace
            required property int index
            required property string name
            required property string workspaceId
            required property bool isActive
            readonly property var map: root.mapFor ? root.mapFor(index) : null
            width: workspaceList.width
            height: root.compact ? 98 : 116
            padding: 12
            Accessible.name: name
            onClicked: root.workspaces.switchTo(workspaceId)
            background: Rectangle {
                radius: Appearance.radius * 0.7
                color: Appearance.surface
                border.width: 1
                border.color: workspace.isActive || workspace.visualFocus ? Appearance.accent : Appearance.outline
            }
            contentItem: ColumnLayout {
                spacing: 9
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 5
                    color: Appearance.recess
                    PlacementMiniature {
                        anchors.fill: parent
                        anchors.margins: 3
                        model: workspace.map
                        interactive: false
                    }
                }
                RowLayout {
                    spacing: 9
                    Text {
                        text: String(workspace.index + 1).padStart(2, "0")
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 11
                        color: Appearance.muted
                    }
                    Text {
                        Layout.fillWidth: true
                        text: workspace.name
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 11
                        color: workspace.isActive ? Appearance.text : Appearance.muted
                        elide: Text.ElideRight
                    }
                    Text {
                        text: workspace.map ? workspace.map.windows.length : ""
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 11
                        color: Appearance.muted
                    }
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
        PlacementMiniature {
            anchors.fill: parent
            visible: !desktop.active
            model: root.map
            interactive: true
            labels: true
            onCellClicked: id => {
                const cell = root.map.cells.find(c => c.id === id);
                if (cell && cell.windowId)
                    root.selectedId = cell.windowId;
                else if (root.selectedWindow)
                    root.map.placeNavigationWindowInZone(root.selectedId, id);
                else
                    root.map.activate(id);
            }
        }
        Repeater {
            model: desktop.active ? root.windows : []
            delegate: AbstractButton {
                id: liveWindow
                required property var modelData
                required property int index
                x: (modelData.x * root.workArea.width - root.canvasLeft) * root.previewRect.width / root.canvas.width
                y: (modelData.y * root.workArea.height - root.canvasTop) * root.previewRect.height / root.canvas.height
                width: modelData.w * root.workArea.width * root.previewRect.width / root.canvas.width
                height: modelData.h * root.workArea.height * root.previewRect.height / root.canvas.height
                visible: !modelData.offscreen && !modelData.minimized
                Accessible.name: modelData.title || modelData.appId
                onClicked: root.select(index)
                onDoubleClicked: root.openSelected()
                background: Rectangle {
                    radius: Appearance.radius
                    color: "transparent"
                    border.width: root.selectedId === liveWindow.modelData.windowId ? 1 : 0
                    border.color: Appearance.windowColor(liveWindow.modelData.colorIndex >= 0 ? liveWindow.modelData.colorIndex : liveWindow.index)
                }
                contentItem: Item {
                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: 15
                        anchors.bottomMargin: 14
                        width: Math.min(parent.width - 20, label.implicitWidth + 20)
                        height: 28
                        radius: 6
                        color: Appearance.card
                        Text {
                            id: label
                            anchors.fill: parent
                            anchors.margins: 4
                            text: root.selectedId === liveWindow.modelData.windowId ? qsTr("Selected · Enter to open") : qsTr("Click to select")
                            font.family: Tokens.font_family_ui
                            font.pixelSize: 10
                            color: Appearance.text
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
    ShellSurface {
        x: root.width - root.inspectorWidth - root.inset + (root.compact ? 0 : 2)
        y: root.previewRect.y
        width: root.inspectorWidth
        height: Math.min(inspector.implicitHeight + 40, root.height - y - 80)
        railT: 0.82
        Flickable {
            anchors.fill: parent
            anchors.margins: 20
            contentWidth: width
            contentHeight: inspector.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: inspector
                width: parent.width
                spacing: 0
                Text {
                    text: qsTr("THIS WORKSPACE")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 9
                    font.letterSpacing: 2
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.topMargin: 8
                    text: qsTr("Shape the space")
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 15
                    font.weight: Font.Medium
                }
                Text {
                    Layout.topMargin: 18
                    text: qsTr("Placement")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 10
                }
                ShellComboBox {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    implicitHeight: 33
                    model: [qsTr("Snapping"), qsTr("Tiling"), qsTr("Scrolling")]
                    currentIndex: root.map ? root.map.mode : -1
                    displayText: currentIndex < 0 ? qsTr("Off") : currentText
                    Accessible.name: qsTr("Placement")
                    onActivated: root.map.setPlacementMode(currentIndex)
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 20
                    text: root.map && root.map.mode === 0 ? qsTr("Place windows into the zones you choose.") : root.scrolling ? qsTr("Windows flow across an open strip. Scroll to find your place.") : qsTr("A main window with a supporting stack.")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 11
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 17.6
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 17
                    Layout.bottomMargin: 17
                    height: 1
                    color: Appearance.outline
                }
                Text {
                    text: qsTr("SELECTED WINDOW")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 9
                    font.letterSpacing: 0.8
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    text: root.selectedApp
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 7
                    text: root.selectedWindow ? root.selectedWindow.title : ""
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 17
                    Layout.bottomMargin: 17
                    height: 1
                    color: Appearance.outline
                }
                Text {
                    text: qsTr("MOVE TO WORKSPACE")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 9
                    font.letterSpacing: 0.8
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    spacing: 7
                    Repeater {
                        model: root.desktopCount
                        ShellButton {
                            required property int index
                            width: 50
                            height: 34
                            text: String(index + 1).padStart(2, "0")
                            label: qsTr("Move to workspace %1").arg(index + 1)
                            outlined: true
                            flat: true
                            labelSize: 10
                            enabled: !!root.selectedWindow && index !== root.currentDesktop
                            onClicked: root.map.moveNavigationWindowToDesktop(root.selectedId, index)
                        }
                    }
                }
                Rectangle {
                    visible: root.map && root.map.mode === 0
                    Layout.fillWidth: true
                    Layout.topMargin: 17
                    Layout.bottomMargin: 17
                    height: 1
                    color: Appearance.outline
                }
                Text {
                    visible: root.map && root.map.mode === 0
                    text: qsTr("PLACE IN ZONE")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 9
                    font.letterSpacing: 0.8
                }
                Flow {
                    visible: root.map && root.map.mode === 0
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    spacing: 7
                    Repeater {
                        model: root.map && root.map.mode === 0 ? root.map.cells : []
                        ShellButton {
                            required property var modelData
                            width: 36
                            height: 34
                            text: String(modelData.zoneNumber)
                            label: qsTr("Place selected window in zone %1").arg(modelData.zoneNumber)
                            labelSize: 10
                            flat: true
                            outlined: true
                            enabled: !!root.selectedWindow
                            onClicked: root.map.placeNavigationWindowInZone(root.selectedId, modelData.id)
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
        y: root.previewRect.y + root.previewRect.height + 8
        width: root.previewRect.width
        height: 36
        visible: root.scrolling
        orientation: ListView.Horizontal
        spacing: 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.windows
        ScrollBar.horizontal: ScrollBar {}
        delegate: ShellButton {
            required property var modelData
            required property int index
            width: 144
            height: 30
            text: (index + 1) + "  " + (modelData.title || modelData.appId)
            labelSize: 10
            highlighted: root.selectedId === modelData.windowId
            onClicked: root.select(index)
            onDoubleClicked: root.openSelected()
        }
    }
    Item {
        x: root.previewRect.x + 2
        y: root.height - Math.min(130, root.height * 0.14)
        width: root.previewRect.width - 4
        height: 20
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
                text: qsTr("Select window")
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
            Keycap {
                text: "Enter"
            }
            Text {
                text: qsTr("Open")
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
            Keycap {
                text: "Esc"
            }
            Text {
                text: qsTr("Return")
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        ShellButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.compact
            text: qsTr("Return to desktop ↗")
            labelSize: 10
            foreground: Appearance.accent
            flat: true
            onClicked: root.closeRequested()
        }
    }
}
