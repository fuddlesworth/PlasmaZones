// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Audio, the default-sink volume bar widget.
//
// Binds to the process-global PipeWireHost singleton. The default sink has
// no direct accessor on the model, so an Instantiator surfaces each sink
// row's PwNode; the row whose name matches defaultSinkName becomes the
// active node, falling back to the first sink when PipeWire publishes no
// default. Scroll adjusts the linear amplitude in 5% steps and left-click
// toggles mute. Volume writes are asynchronous, so the readout updates when
// PipeWire echoes the new value.

import QtQuick
import QtQml
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Service.PipeWire

Item {
    id: root

    // Gate on a RESOLVED sink, not merely a live PipeWire connection: with
    // no sink there is no value to show, and a literal "0%" would be a
    // false readout rather than an absent one.
    readonly property bool available: root.node !== null

    // The sink whose volume the bar shows: the default sink when PipeWire
    // publishes one, otherwise the first sink present. The fallback matters
    // because `default.audio.sink` comes from the metadata module, and a
    // session without it (or before it publishes) would otherwise hide the
    // widget for its whole life on a box with perfectly good sinks.
    readonly property PwNode node: root._defaultNode ? root._defaultNode : root._firstNode

    // Matched against PipeWireHost.defaultSinkName by the Instantiator below.
    property PwNode _defaultNode: null
    // Row 0, tracked so the fallback has something to point at.
    property PwNode _firstNode: null
    readonly property int volumePercent: root.node && root.node.volumes.length > 0 ? Math.round(root.node.volumes[0] * 100) : 0
    readonly property bool muted: root.node ? root.node.muted : false

    visible: root.available
    implicitWidth: root.available ? row.implicitWidth : 0
    implicitHeight: row.implicitHeight

    PwSinkModel {
        id: sinks

        connection: PipeWireHost.connection
    }

    // PwNodeModel exposes no row accessor, so each row's node is surfaced
    // through a non-visual delegate: one reports the default match, row 0
    // reports itself as the fallback.
    Instantiator {
        model: sinks

        delegate: QtObject {
            required property PwNode node
            required property string name
            required property int index

            readonly property bool isDefault: name.length > 0 && name === PipeWireHost.defaultSinkName

            onIsDefaultChanged: root._syncDefault(isDefault, node)
            // index, not just completion: when row 0 is removed the model
            // renumbers the survivors in place rather than re-running their
            // completion, so tracking only onCompleted would leave the
            // fallback null for the rest of the session.
            onIndexChanged: root._syncFirst(index, node)
            Component.onCompleted: {
                root._syncDefault(isDefault, node);
                root._syncFirst(index, node);
            }
            Component.onDestruction: {
                if (root._defaultNode === node)
                    root._defaultNode = null;
                if (root._firstNode === node)
                    root._firstNode = null;
            }
        }
    }

    // A delegate pushes when it becomes the default and clears itself when
    // it stops being one, so a default that moves between sinks (or goes
    // away entirely) leaves _defaultNode pointing at the right row.
    function _syncDefault(isDefault, node) {
        if (isDefault)
            root._defaultNode = node;
        else if (root._defaultNode === node)
            root._defaultNode = null;
    }

    // Same shape as _syncDefault, for the row-0 fallback: claim the slot on
    // becoming index 0, release it on moving away.
    function _syncFirst(index, node) {
        if (index === 0)
            root._firstNode = node;
        else if (root._firstNode === node)
            root._firstNode = null;
    }

    function _toggleMute() {
        if (root.node)
            root.node.setMuted(!root.muted);
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
            // Folded into the root's Accessible.name already; QQuickText
            // exposes itself as its own StaticText node, so without this
            // assistive tech reads the composed name and then re-reads
            // this fragment.
            Accessible.ignored: true
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

        // The readout carries the value on the root Indicator; this area is
        // the actionable control, so it announces AND performs the toggle.
        // Pointer + assistive tech only, no keyboard leg: this widget is an
        // indicator that happens to be clickable, and the bar's panel takes
        // no keyboard focus, so there is nothing to Tab from. BarIconButton
        // carries the full quad because it is the shared button atom and is
        // meant to work wherever a focused surface hosts it.
        Accessible.role: Accessible.Button
        Accessible.name: root.muted ? "Unmute" : "Mute"
        Accessible.onPressAction: root._toggleMute()
        // The wheel is the only pointer path to the volume itself, so give
        // assistive tech the same reach rather than leaving it mute-only.
        Accessible.onIncreaseAction: root._adjust(1)
        Accessible.onDecreaseAction: root._adjust(-1)

        onClicked: root._toggleMute()
        // Only a vertical wheel adjusts. A horizontal scroll or trackpad
        // tilt delivers angleDelta.y === 0, which would otherwise fall to
        // the negative branch and silently lower the volume.
        onWheel: wheel => {
            if (wheel.angleDelta.y !== 0)
                root._adjust(wheel.angleDelta.y > 0 ? 1 : -1);
        }
    }
}
