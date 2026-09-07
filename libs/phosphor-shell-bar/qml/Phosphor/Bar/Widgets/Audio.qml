// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Audio, the default-sink volume bar widget.
//
// Binds to the process-global PipeWireHost singleton, which resolves
// `defaultSink` by joining the default-sink name against the live node
// set. Falls back to the first sink when PipeWire publishes no default.
//
// GLYPH ONLY, no percentage. A2 §5 groups this chip with network,
// bluetooth and battery as one run of four glyphs where state is
// brightness, and battery is the single argued exception to that (its
// number is the one people actually check). A volume readout on the bar
// spends width to answer a question the OSD already answers on every
// change.
//
// Scroll adjusts the linear amplitude in 5% steps, left-click opens
// AudioPanel and MIDDLE-click toggles mute. Volume writes are
// asynchronous, so the readout updates when PipeWire echoes the new value.
//
// Left-click opens the panel rather than muting because every other status
// chip in the bar opens its panel on a left-click, and one chip that
// silences the machine instead is the kind of inconsistency people find by
// accident. Mute keeps a pointer path (middle-click) and both assistive
// actions below, so nothing became unreachable.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Service.PipeWire

BarWidget {
    id: root

    /// Relayed by BarController as BarRegistry.widgetActivated("audio").
    /// See Network.qml for why this is declared per widget rather than on
    /// BarWidget.
    signal activated

    // Gate on a RESOLVED sink, not merely a live PipeWire connection: with
    // no sink there is no value to show, and a literal "0%" would be a
    // false readout rather than an absent one.
    available: root.node !== null
    contentWidth: root.iconSize
    contentHeight: root.iconSize

    readonly property int iconSize: 18

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

    // The percentage is no longer drawn, so assistive tech is now the only
    // place it is spoken. Keep it here even though the glyph carries the
    // coarse state, or the value becomes unreachable without opening the
    // panel.
    Accessible.role: Accessible.Indicator
    Accessible.name: root.muted ? qsTr("Volume muted") : qsTr("Volume %1 percent").arg(root.volumePercent)

    Kirigami.Icon {
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        source: root.muted || root.volumePercent === 0 ? "audio-volume-muted" : root.volumePercent < 34 ? "audio-volume-low" : root.volumePercent < 67 ? "audio-volume-medium" : "audio-volume-high"
        isMask: true
        // State is brightness, not colour: muted sits back at 35%, live
        // sits at 90%, and the chip never paints a background (A2 §5).
        color: Theme.on_surface
        opacity: root.muted ? 0.35 : 0.9
        scale: pressGuard.pressed ? 0.94 : 1

        Behavior on opacity {
            NumberAnimation {
                duration: Motion.duration_enter
                easing: Motion.enter
            }
        }
        Behavior on scale {
            NumberAnimation {
                duration: Motion.duration_tick
                easing: Motion.reveal
            }
        }
    }

    MouseArea {
        id: pressGuard

        anchors.fill: parent
        // Left opens the panel, middle mutes. Both arrive here rather than
        // splitting the wheel across a MouseArea and the press across a
        // TapHandler: onWheel lives on MouseArea, and two overlapping input
        // items would make which one wins a matter of stacking order.
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
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
        // The press action is what the left button does, so assistive tech
        // and the pointer agree on the primary action. Mute stays reachable
        // through the toggle action below.
        Accessible.name: qsTr("Show audio panel")
        Accessible.onPressAction: root.activated()
        Accessible.onToggleAction: root._toggleMute()
        // The wheel is the only pointer path to the volume itself, so the
        // adjust actions are exposed here too. Increase/decrease belong to
        // the slider vocabulary and not every AT bridge surfaces them on a
        // Button role, so this widens reach where the bridge allows it
        // rather than guaranteeing it.
        Accessible.onIncreaseAction: root._adjust(1)
        Accessible.onDecreaseAction: root._adjust(-1)

        onClicked: mouse => {
            if (mouse.button === Qt.MiddleButton)
                root._toggleMute();
            else
                root.activated();
        }
        // Only a vertical wheel adjusts. A horizontal scroll or trackpad
        // tilt delivers angleDelta.y === 0, which would otherwise fall to
        // the negative branch and silently lower the volume.
        onWheel: wheel => {
            if (wheel.angleDelta.y !== 0)
                root._adjust(wheel.angleDelta.y > 0 ? 1 : -1);
        }
    }
}
