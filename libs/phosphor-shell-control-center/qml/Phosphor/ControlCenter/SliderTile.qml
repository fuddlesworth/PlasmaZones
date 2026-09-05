// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.ControlCenter.SliderTile, the chrome for one continuous rail.
//
// The companion to Tile: a control with a range (volume, brightness).
// Same 52 px rail, but the 2 px underline along the bottom IS the slider
// (A3 §2b): its length is the value and its colour is the state-axis
// sample of that value, cyan low to rose at the limit. The whole rail is
// the drag surface; the wheel steps it.
//
//   SliderTile {
//       iconName: "audio-volume-high"
//       label: qsTr("Volume")
//       value: sink.volumePercent
//       onMoved: v => sink.setVolume(v / 100)
//       muted: sink.muted
//       onIconActivated: sink.setMuted(!sink.muted)
//   }
//
// Like Tile, this does not latch: `moved` asks the service to change and
// `value` follows the service's echo, so a clamped or quantised request
// never leaves the line somewhere the hardware never went.

import QtQuick
import QtQuick.Layouts
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
    readonly property bool hasDetail: root.detailEnabled && root.detailContent !== null
    readonly property bool spansRow: true

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
    implicitHeight: Math.max(52, content.implicitHeight + Tokens.spacing_m)
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

    HoverHandler {
        id: hover

        enabled: root.available
        cursorShape: Qt.SizeHorCursor
    }

    // The whole rail is the drag surface.
    DragHandler {
        id: drag

        enabled: root.available
        target: null
        yAxis.enabled: false
        onActiveChanged: {
            if (active)
                root._moveTo(centroid.position.x);
        }
        onCentroidChanged: {
            if (active)
                root._moveTo(centroid.position.x);
        }
    }
    TapHandler {
        enabled: root.available
        onTapped: eventPoint => root._moveTo(eventPoint.position.x)
    }
    WheelHandler {
        enabled: root.available
        onWheel: event => root._step(event.angleDelta.y > 0 ? 1 : -1)
    }

    RowLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Tokens.spacing_xs
        anchors.rightMargin: root.hasDetail ? chevron.width + Tokens.spacing_s : Tokens.spacing_xs
        spacing: Tokens.spacing_m

        Item {
            id: iconButton

            implicitWidth: 16
            implicitHeight: 16
            Layout.alignment: Qt.AlignVCenter

            Accessible.role: root.hasIconAction ? Accessible.Button : Accessible.Graphic
            Accessible.ignored: !root.hasIconAction
            Accessible.name: root.muted ? qsTr("Unmute") : qsTr("Mute")
            Accessible.onPressAction: root._activateIcon()

            activeFocusOnTab: root.available && root.hasIconAction
            Keys.onSpacePressed: event => root._activateIconFromKey(event)
            Keys.onReturnPressed: event => root._activateIconFromKey(event)
            Keys.onEnterPressed: event => root._activateIconFromKey(event)

            Kirigami.Icon {
                anchors.fill: parent
                source: root.iconName
                isMask: true
                color: Theme.on_surface
                opacity: root.muted ? 0.45 : 1
            }

            HoverHandler {
                enabled: root.available && root.hasIconAction
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                enabled: root.available && root.hasIconAction
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: root._activateIcon()
            }
        }

        Text {
            Accessible.ignored: true
            text: root.label
            color: Theme.on_surface
            font.family: Tokens.font_family_ui
            font.pixelSize: Tokens.font_size_body_l
            font.weight: Tokens.font_weight_medium
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        TabularText {
            Accessible.ignored: true
            text: root._readout
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_m
            tickOnChange: true
            t: root._fraction
        }
    }

    // Track: the resting line under the whole rail.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        color: Theme.on_surface
        opacity: 0.12
    }

    // The slider: length is the value, colour is the value.
    SpectrumUnderline {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        length: root.width
        value: root.muted ? 0 : root._fraction
        t: root._fraction
        opacity: hover.hovered || drag.active ? 1 : 0.9
    }

    // The knob shows only while the rail is being aimed at.
    Rectangle {
        x: root.width * root._fraction - width / 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: -3
        width: 8
        height: 8
        radius: 4
        color: Spectrum.focus
        opacity: (hover.hovered || drag.active) && !root.muted ? 0.9 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: hover.hovered ? Motion.duration_enter : Motion.duration_release
                easing: hover.hovered ? Motion.enter : Motion.release
            }
        }
    }

    SpectrumStroke {
        anchors.fill: parent
        radius: Tokens.radius_edge
        focused: root.activeFocus
        visible: root.activeFocus
        opacity: 0
    }

    Item {
        id: chevron

        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        height: 28
        visible: root.hasDetail

        Kirigami.Icon {
            anchors.centerIn: parent
            width: 16
            height: 16
            source: "go-next-symbolic"
            isMask: true
            color: Theme.on_surface
            opacity: 0.7
        }

        HoverHandler {
            enabled: root.available && root.hasDetail
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            enabled: root.available && root.hasDetail
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: root.detailRequested()
        }
    }
}
