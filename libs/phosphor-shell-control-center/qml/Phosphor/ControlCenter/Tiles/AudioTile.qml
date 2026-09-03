// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.ControlCenter.AudioTile, the default-sink volume slider.
//
// Binds to the process-global PipeWireHost singleton and drives the
// default sink's linear amplitude. Tapping the glyph toggles mute; the
// chevron opens a device picker (the detail view).
//
// PipeWire echoes a volume write back through propsChanged, so the handle
// follows what the server actually applied rather than where the pointer
// was released. That is the whole reason SliderTile does not latch.

import QtQuick
import Phosphor.ControlCenter
import Phosphor.Service.PipeWire

SliderTile {
    id: root

    // The sink whose volume this tile drives: the default sink when
    // PipeWire publishes one, otherwise the first sink present. The
    // fallback matters because `default.audio.sink` comes from the
    // metadata module, and a session without it (or before it publishes)
    // would otherwise leave the tile inert on a box with working sinks.
    //
    // `sinks.firstNode`, not `sinks.nodeAt(0)`: the latter is a plain
    // function call that tracks no dependency, and pairing it with `count`
    // does not fix that. A PipeWire restart landing on the same number of
    // sinks replaces every node without moving the count, so a count-gated
    // binding would drive a node owned by the destroyed connection for the
    // rest of the session. Same reasoning as the bar's Audio widget.
    readonly property PwNode node: PipeWireHost.defaultSink ? PipeWireHost.defaultSink : sinks.firstNode

    readonly property real _amplitude: root.node && root.node.volumes.length > 0 ? root.node.volumes[0] : 0

    PwSinkModel {
        id: sinks

        connection: PipeWireHost.connection
    }

    iconName: {
        if (root.muted || root.value === 0)
            return "audio-volume-muted";
        if (root.value < 34)
            return "audio-volume-low";
        return root.value < 67 ? "audio-volume-medium" : "audio-volume-high";
    }
    label: qsTr("Volume")
    from: 0
    to: 100
    value: Math.round(root._amplitude * 100)
    muted: root.node ? root.node.muted : false
    // Gate on a RESOLVED sink, not merely a live PipeWire connection: with
    // no sink there is nothing to drive, and a slider sitting at zero would
    // be a false readout rather than an absent one.
    available: root.node !== null
    hasDetail: true

    onMoved: v => {
        if (!root.node)
            return;
        // Clamp to the linear-amplitude mixer range; PwNode forwards the
        // value verbatim, so the UI owns the clamp.
        root.node.setVolume(Math.max(0, Math.min(1, v / 100)));
    }

    onIconActivated: {
        if (root.node)
            root.node.setMuted(!root.node.muted);
    }
}
