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

    readonly property bool hasDetail: root.detailEnabled && (root.detailContent !== null || root.detailPanelId !== "")
    // A level spans the grid: its underline is the control, and a longer
    // line is a finer one to drag.
    property bool spansRow: true
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
    implicitHeight: Appearance.compact ? 68 : 78
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
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 2
        spacing: 10
        ShellButton {
            implicitWidth: 34
            implicitHeight: 34
            iconName: root.iconName
            label: root.hasIconAction ? (root.muted ? qsTr("Unmute") : qsTr("Mute")) : root.label
            enabled: root.available && root.hasIconAction
            onClicked: root._activateIcon()
        }
        Text {
            Layout.fillWidth: true
            text: root.label
            color: Appearance.text
            font.family: Tokens.font_family_ui
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        Text {
            text: root._readout
            color: Appearance.muted
            font.family: Tokens.font_family_mono
            font.pixelSize: 12
        }
        ShellButton {
            visible: root.hasDetail
            iconName: "go-next"
            label: qsTr("%1 details").arg(root.label)
            implicitWidth: 34
            implicitHeight: 34
            enabled: root.available
            onClicked: root.detailRequested()
        }
    }
    Slider {
        id: slider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 30
        from: root.from
        to: root.to
        value: root.value
        enabled: root.available
        Accessible.name: root.label
        onMoved: root.moved(value)
        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 8
            radius: 4
            color: Appearance.card
            Rectangle {
                width: parent.width * slider.visualPosition
                height: parent.height
                radius: parent.radius
                color: root.muted ? Appearance.muted : Appearance.at(root.railT)
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 16
            height: 16
            radius: 8
            color: Appearance.light ? Appearance.accent : Appearance.text
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
}
