// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.AudioPanel, the audio chip's panel.
//
// The default sink and the default source get a slider each, then every
// playback stream gets its own, so a browser tab can be turned down
// without touching the master. Volumes are linear amplitude, the same
// range the bar widget's scroll steps through.
//
// No output-device picker. Switching the default sink is a write to
// PipeWire's metadata module, which PipeWireHost does not expose:
// `defaultSinkName` is read-only. A picker whose rows did nothing would be
// worse than not offering one, so this panel controls the devices that
// ARE default and says which they are.
//
// Every write is asynchronous. The readouts move when PipeWire echoes the
// new value back through propsChanged, not when the slider is released, so
// what is on screen is the daemon's state rather than the request.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.PipeWire

PanelFrame {
    id: root

    title: qsTr("Audio")
    iconName: root._sinkMuted || root._sinkPercent === 0 ? "audio-volume-muted" : "audio-volume-high"
    subtitle: {
        if (!PipeWireHost.connected)
            return qsTr("PipeWire is not running");
        if (!root._sink)
            return qsTr("No output device");
        return root._deviceLabel(root._sink);
    }

    readonly property PwNode _sink: PipeWireHost.defaultSink ?? sinks.firstNode
    readonly property PwNode _source: PipeWireHost.defaultSource ?? sources.firstNode

    readonly property int _sinkPercent: root._sink ? Math.round(root._volumeOf(root._sink) * 100) : 0
    readonly property bool _sinkMuted: root._sink ? root._sink.muted : false

    // `firstNode`, never `nodeAt(0)`: the latter is a plain function call
    // that tracks no dependency, and pairing it with `count` does not fix
    // that — a PipeWire restart landing on the same number of nodes
    // replaces every one of them without moving the count.
    PwSinkModel {
        id: sinks

        connection: PipeWireHost.connection
    }

    PwSourceModel {
        id: sources

        connection: PipeWireHost.connection
    }

    // The per-application playback streams. PwStreamModel is pinned to
    // "Stream/Output/Audio", which is what PipeWire calls a client PLAYING
    // audio: output from the app's point of view, into a sink. Recording
    // streams are not in it, which suits a panel about what you hear.
    //
    // The pinned subclasses rather than PwNodeModel with a mediaClasses
    // filter: the base type is registered uncreatable from QML precisely so
    // callers take these.
    PwStreamModel {
        id: streams

        connection: PipeWireHost.connection
    }

    /// A node's volume as a single number. PipeWire carries one amplitude
    /// per channel; the panel drives them together, so the first channel is
    /// the one shown and a node reporting none reads as silent.
    function _volumeOf(node) {
        if (!node || node.volumes.length === 0)
            return 0;
        return node.volumes[0];
    }

    /// The friendliest name a node has. `description` is the human string
    /// ("Built-in Audio Analogue Stereo"); `nick` is shorter and often
    /// absent; `name` is the machine id and the last resort.
    function _deviceLabel(node) {
        if (!node)
            return "";
        if (node.description.length > 0)
            return node.description;
        return node.nick.length > 0 ? node.nick : node.name;
    }

    VolumeRow {
        width: parent.width
        node: root._sink
        iconName: root._sinkMuted || root._sinkPercent === 0 ? "audio-volume-muted" : "audio-volume-high"
        label: qsTr("Output")
        sublabel: root._deviceLabel(root._sink)
    }

    VolumeRow {
        width: parent.width
        visible: root._source !== null
        node: root._source
        iconName: root._source && root._source.muted ? "microphone-sensitivity-muted" : "audio-input-microphone"
        label: qsTr("Input")
        sublabel: root._deviceLabel(root._source)
    }

    Text {
        width: parent.width
        visible: streams.count > 0
        text: qsTr("Applications")
        color: Theme.on_surface_variant
        font.pixelSize: Tokens.font_size_label_s
        font.family: Tokens.font_family_ui
        topPadding: Tokens.spacing_s
    }

    Repeater {
        model: streams

        // The delegate is a wrapper rather than a VolumeRow directly: the
        // model's role is called `node` and so is VolumeRow's property, and
        // a required property of that name on the row itself would collide
        // with the one it is meant to feed.
        delegate: Item {
            id: streamRow

            required property var node

            width: parent ? parent.width : 0
            implicitHeight: streamVolume.implicitHeight

            VolumeRow {
                id: streamVolume

                width: streamRow.width
                node: streamRow.node
                iconName: "audio-volume-high"
                label: root._deviceLabel(streamRow.node)
            }
        }
    }
}
