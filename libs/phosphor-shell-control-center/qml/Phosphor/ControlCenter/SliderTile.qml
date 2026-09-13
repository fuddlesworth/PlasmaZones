// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Service-controlled level with separate mute, details and slider targets.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    property string iconName: ""
    property string label: ""
    property real value: 0
    property real from: 0
    property real to: 100
    property string readout: ""
    property bool muted: false
    property bool available: true
    property string detailTitle: ""
    property Component detailContent: null
    property bool detailEnabled: true
    /// The bar panel this row drills into, by the bar-widget id that opens
    /// it. Mirrors Tile.detailPanelId, and exists on BOTH because a tile
    /// and a slider are separate types: the control center sets this on
    /// whichever it is handed, and assigning a property one of them lacks
    /// is not a warning — the file fails to compile and the tile vanishes
    /// from the grid, which is exactly what happened to the volume row.
    property string detailPanelId: ""
    property string deviceName: ""

    readonly property bool hasDetail: root.detailEnabled && (root.detailContent !== null || root.detailPanelId !== "")
    // A level spans the grid: its underline is the control, and a longer
    // line is a finer one to drag.
    property bool spansRow: true
    property string controlGroup: "levels"
    property real railT: 0.5

    signal moved(real value)
    signal iconActivated
    property bool hasIconAction: false
    signal detailRequested

    function _activateIcon(): void {
        if (root.available && root.hasIconAction)
            root.iconActivated();
    }

    function _activateIconFromKey(event: var): void {
        if (event.isAutoRepeat)
            return;
        root._activateIcon();
        event.accepted = true;
    }

    implicitWidth: 320
    implicitHeight: hasDetail ? 105 : 59
    opacity: root.available ? 1 : StateLayer.disabled_content

    readonly property real _fraction: root.to > root.from ? Math.max(0, Math.min(1, (root.value - root.from) / (root.to - root.from))) : 0
    readonly property string _readout: {
        if (root.readout !== "")
            return root.readout;
        if (root.to <= root.from)
            return "";
        return Math.round(100 * root._fraction) + "%";
    }

    Accessible.role: Accessible.Slider
    Accessible.name: root.label
    Accessible.description: root._readout

    activeFocusOnTab: root.available
    Keys.onLeftPressed: root._step(-1)
    Keys.onRightPressed: root._step(1)

    function _step(direction: int): void {
        if (!root.available)
            return;
        const span = root.to - root.from;
        root.moved(Math.max(root.from, Math.min(root.to, root.value + direction * span / 20)));
    }

    function _moveTo(x: real): void {
        if (!root.available || root.width <= 0)
            return;
        const f = Math.max(0, Math.min(1, x / root.width));
        root.moved(root.from + f * (root.to - root.from));
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 18
        spacing: 8
        AbstractButton {
            implicitWidth: 15
            implicitHeight: 18
            enabled: root.available && root.hasIconAction
            Accessible.name: root.muted ? qsTr("Unmute") : qsTr("Mute")
            onClicked: root._activateIcon()
            contentItem: ShellIcon {
                source: root.iconName
                isMask: true
                color: Appearance.muted
            }
        }
        Text {
            Layout.fillWidth: true
            text: root.label
            color: Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: 10
        }
        Text {
            text: root.available ? root._readout : qsTr("Unavailable")
            color: Appearance.text
            font.family: Tokens.font_family_mono
            font.pixelSize: 10
        }
    }
    Slider {
        id: slider
        anchors.left: parent.left
        anchors.right: parent.right
        y: 31
        height: 22
        padding: 0
        from: root.from
        to: root.to
        value: root.value
        enabled: root.available
        Accessible.name: root.label
        onMoved: root.moved(value)
        background: Rectangle {
            y: (slider.height - height) / 2
            width: slider.width
            height: 10
            radius: 5
            color: Appearance.recess
            Rectangle {
                width: parent.width * slider.visualPosition
                height: parent.height
                radius: parent.radius
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop {
                        position: 0
                        color: root.muted ? Appearance.muted : Appearance.stops[0]
                    }
                    GradientStop {
                        position: 1
                        color: root.muted ? Appearance.muted : Appearance.stops[2]
                    }
                }
            }
        }
        handle: Rectangle {
            x: slider.visualPosition * (slider.width - width)
            y: (slider.height - height) / 2
            width: 7
            height: 22
            radius: 3
            color: Appearance.text
            border.width: slider.visualFocus ? 2 : 0
            border.color: Appearance.accent
        }
        WheelHandler {
            enabled: root.available
            onWheel: event => {
                const delta = event.angleDelta.x || event.angleDelta.y;
                if (delta !== 0)
                    root._step(delta > 0 ? 1 : -1);
            }
        }
    }
    AbstractButton {
        y: 70
        width: parent.width
        height: 28
        visible: root.hasDetail
        enabled: root.available
        Accessible.name: qsTr("Change output")
        onClicked: root.detailRequested()
        contentItem: RowLayout {
            Text {
                Layout.fillWidth: true
                text: root.deviceName
                font.family: Tokens.font_family_ui
                font.pixelSize: 9
                color: Appearance.muted
                elide: Text.ElideRight
            }
            Text {
                text: qsTr("Change output ›")
                font.family: Tokens.font_family_ui
                font.pixelSize: 9
                color: Appearance.muted
            }
        }
    }
}
