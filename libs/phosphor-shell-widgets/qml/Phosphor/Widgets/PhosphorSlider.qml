// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.PhosphorSlider, M3 continuous slider.
//
// A token-driven horizontal slider: inactive track, active (filled)
// track, and a draggable handle. Drag the handle or tap anywhere on the
// track to set the value. User-driven changes are clamped to [from, to]
// and `moved` fires whenever the user changes it. The host owns range
// validity for programmatic `value` assignments (see the README note).
//
//   PhosphorSlider {
//       from: 0; to: 100; value: volume
//       onMoved: (v) => audio.setVolume(v)
//   }
//
// The handle animates to programmatic value changes but follows the
// pointer instantly while dragging, so user input never feels laggy.

import QtQuick
import Phosphor.Theme

Item {
    id: root

    property real from: 0
    property real to: 1
    property real value: 0
    // Arrow-key step. 0 means "derive a sensible default" (a twentieth of
    // the range), so a keyboard user gets reasonable increments without the
    // host having to set one per range.
    property real stepSize: 0

    // Emitted with the new value on every user-driven change (drag, track
    // tap, or arrow key), not on programmatic `value` assignments.
    signal moved(real value)

    implicitWidth: 200
    implicitHeight: 40

    readonly property real _step: stepSize > 0 ? stepSize : (to - from) / 20

    // Keyboard: Tab-focusable when enabled; Left/Down nudge down, Right/Up
    // nudge up, Home/End jump to the ends.
    activeFocusOnTab: enabled
    Keys.onPressed: event => {
        if (!root.enabled)
            return;
        switch (event.key) {
        case Qt.Key_Left:
        case Qt.Key_Down:
            root._setValue(root.value - root._step);
            event.accepted = true;
            break;
        case Qt.Key_Right:
        case Qt.Key_Up:
            root._setValue(root.value + root._step);
            event.accepted = true;
            break;
        case Qt.Key_Home:
            root._setValue(root.from);
            event.accepted = true;
            break;
        case Qt.Key_End:
            root._setValue(root.to);
            event.accepted = true;
            break;
        }
    }

    // Clamp to [from, to] and emit moved() only on a real change. Shared by
    // the keyboard path; pointer drag/tap go through _setFromX.
    function _setValue(v) {
        if (to <= from)
            return;
        const clamped = Math.max(from, Math.min(to, v));
        if (clamped !== value) {
            value = clamped;
            moved(clamped);
        }
    }

    Accessible.role: Accessible.Slider
    // Include the value in the name: QML's Accessible attached type exposes
    // role/name but not the numeric value/minimumValue/maximumValue
    // interface (those are C++-only), so the position is folded into the
    // announced name instead.
    Accessible.name: qsTr("Slider, %1").arg(Math.round(root.value))

    // Normalised 0..1 position of the current value. Guards a non-positive
    // range (from == to, or an inverted from > to) so it collapses to 0
    // instead of producing NaN or a negative ratio.
    readonly property real _ratio: to > from ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0
    // Travel available to the handle centre: the full width minus the
    // handle so it never overhangs either end.
    readonly property real _trackWidth: width - handle.width

    // Map a pointer x (in root coordinates) to a clamped value and emit
    // moved() if it actually changed.
    function _setFromX(px) {
        // Mirror _ratio's guard: with no usable track or a non-positive
        // range (to <= from) the handle is pinned at 0 and can't reflect a
        // value, so don't emit one the user can't see.
        if (_trackWidth <= 0 || to <= from)
            return;
        const r = Math.max(0, Math.min(1, (px - handle.width / 2) / _trackWidth));
        const v = from + r * (to - from);
        if (v !== value) {
            value = v;
            moved(v);
        }
    }

    readonly property color _disabledTint: StateLayer.disabledContent(Theme.on_surface)

    // Inactive track.
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        x: handle.width / 2
        width: root._trackWidth
        height: 4
        radius: height / 2
        color: root.enabled ? Theme.surface_variant : StateLayer.disabledContainer(Theme.on_surface)
    }

    // Active (filled) track.
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        x: handle.width / 2
        width: root._trackWidth * root._ratio
        height: 4
        radius: height / 2
        color: root.enabled ? Theme.primary : root._disabledTint
    }

    // Focus halo behind the handle: the M3 focus indicator when the slider
    // is reached by Tab. Declared before the handle so it paints behind it.
    Rectangle {
        anchors.centerIn: handle
        width: handle.width + 2 * Tokens.spacing_s
        height: width
        radius: width / 2
        color: Theme.primary
        opacity: root.activeFocus ? StateLayer.focus : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation {
                duration: Motion.duration_short_2
                easing: Motion.standard
            }
        }
    }

    Rectangle {
        id: handle

        width: 20
        height: 20
        radius: width / 2
        anchors.verticalCenter: parent.verticalCenter
        x: root._trackWidth * root._ratio
        color: root.enabled ? Theme.primary : root._disabledTint

        // Animate to programmatic value changes, but not while the user
        // drags: the handle must track the pointer with no easing lag.
        Behavior on x {
            enabled: !drag.active
            NumberAnimation {
                duration: Motion.duration_short_2
                easing: Motion.standard
            }
        }
    }

    DragHandler {
        id: drag

        enabled: root.enabled
        target: null
        onCentroidChanged: {
            if (active)
                root._setFromX(centroid.position.x);
        }
    }

    TapHandler {
        enabled: root.enabled
        onTapped: root._setFromX(point.position.x)
    }
}
