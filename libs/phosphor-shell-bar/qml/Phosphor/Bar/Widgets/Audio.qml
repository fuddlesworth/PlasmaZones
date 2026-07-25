// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Audio, the default-sink volume bar widget.
//
// Binds to the process-global PipeWireHost singleton. The default sink
// has no direct accessor on the model, so an Instantiator surfaces each
// sink row's PwNode and the one whose name matches defaultSinkName is
// bound as the active node. Scroll adjusts the linear amplitude in 5%
// steps; left-click toggles mute. Volume writes are asynchronous (the
// readout updates when PipeWire echoes the new value).

import QtQuick
import QtQml
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Service.PipeWire

Item {
    id: root

    readonly property bool _ready: PipeWireHost.connected

    // The default sink's PwNode. Typed so QML nulls it automatically if
    // the underlying node is destroyed (device unplugged).
    property QtObject node: null
    readonly property int volumePercent: root.node && root.node.volumes.length > 0 ? Math.round(root.node.volumes[0] * 100) : 0
    readonly property bool muted: root.node ? root.node.muted : false

    visible: root._ready
    implicitWidth: root._ready ? row.implicitWidth : 0
    implicitHeight: row.implicitHeight

    PwSinkModel {
        id: sinks

        connection: PipeWireHost.connection
    }

    // Locate the default sink node by matching defaultSinkName against the
    // sink rows; PwNodeModel exposes no row accessor, so each row's node
    // is surfaced through a non-visual delegate and the match is bound.
    Instantiator {
        model: sinks

        delegate: QtObject {
            required property var node
            required property string name

            readonly property bool isDefault: name === PipeWireHost.defaultSinkName

            onIsDefaultChanged: if (isDefault)
                root.node = node
            Component.onCompleted: if (isDefault)
                root.node = node
        }
    }

    // A delegate only pushes when it becomes the default; clear the bound
    // node here when the default name moves away from it.
    Connections {
        target: PipeWireHost

        function onDefaultSinkNameChanged() {
            if (root.node && root.node.name !== PipeWireHost.defaultSinkName)
                root.node = null;
        }
    }

    function _adjust(steps) {
        if (!root.node)
            return;
        const cur = root.node.volumes.length > 0 ? root.node.volumes[0] : 0;
        // Clamp to the linear-amplitude mixer range; PwNode forwards the
        // value verbatim, so the UI owns the clamp.
        root.node.setVolume(Math.max(0, Math.min(1, cur + steps * 0.05)));
    }

    Accessible.role: Accessible.Indicator
    Accessible.name: root.muted ? "Volume muted" : "Volume " + root.volumePercent + " percent"

    Row {
        id: row

        spacing: Tokens.spacing_xs

        Kirigami.Icon {
            width: 18
            height: 18
            source: root.muted || root.volumePercent === 0 ? "audio-volume-muted" : root.volumePercent < 34 ? "audio-volume-low" : root.volumePercent < 67 ? "audio-volume-medium" : "audio-volume-high"
            isMask: true
            color: root.muted ? Theme.on_surface_variant : Theme.on_surface
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: root.volumePercent + "%"
            color: root.muted ? Theme.on_surface_variant : Theme.on_surface
            font.pixelSize: Tokens.font_size_label_l
            font.family: Tokens.font_family
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        enabled: root.node !== null
        onClicked: if (root.node)
            root.node.setMuted(!root.muted)
        onWheel: wheel => root._adjust(wheel.angleDelta.y > 0 ? 1 : -1)
    }
}
