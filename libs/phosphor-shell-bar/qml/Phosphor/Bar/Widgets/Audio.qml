// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Audio, the default-sink volume bar widget.
//
// Binds to the process-global PipeWireHost singleton, which resolves
// `defaultSink` by joining the default-sink name against the live node
// set. Falls back to the first sink when PipeWire publishes no default.
// Scroll adjusts the linear amplitude in 5% steps and left-click toggles
// mute. Volume writes are asynchronous, so the readout updates when
// PipeWire echoes the new value.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Service.PipeWire

BarWidget {
    id: root

    // Gate on a RESOLVED sink, not merely a live PipeWire connection: with
    // no sink there is no value to show, and a literal "0%" would be a
    // false readout rather than an absent one.
    available: root.node !== null
    contentWidth: row.implicitWidth
    contentHeight: row.implicitHeight

    // The sink whose volume the bar shows: the default sink when PipeWire
    // publishes one, otherwise the first sink present. The fallback matters
    // because `default.audio.sink` comes from the metadata module, and a
    // session without it (or before it publishes) would otherwise hide the
    // widget for its whole life on a box with perfectly good sinks.
    //
    // `sinks.firstNode`, not `sinks.nodeAt(0)`: the latter is a plain
    // function call that tracks no dependency, and pairing it with `count`
    // does not fix that. A PipeWire restart that lands on the same number
    // of sinks replaces every node without moving the count, so a
    // count-gated binding would stay pinned to a node owned by the
    // destroyed connection and this widget would drive a dead sink for the
    // rest of the session.
    readonly property PwNode node: PipeWireHost.defaultSink ? PipeWireHost.defaultSink : sinks.firstNode

    readonly property int volumePercent: root.node && root.node.volumes.length > 0 ? Math.round(root.node.volumes[0] * 100) : 0
    readonly property bool muted: root.node ? root.node.muted : false

    // Kept solely to back the first-sink fallback above. The default-sink
    // path needs no model at all now that the host resolves it.
    PwSinkModel {
        id: sinks

        connection: PipeWireHost.connection
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
