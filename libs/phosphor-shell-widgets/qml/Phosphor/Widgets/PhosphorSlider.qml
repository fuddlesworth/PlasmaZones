// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.PhosphorSlider, M3 continuous slider.
//
// A token-driven horizontal slider: inactive track, active (filled)
// track, and a draggable handle. Drag the handle or tap anywhere on the
// track to set the value. The value is clamped to [from, to]; `moved`
// fires whenever the user changes it.
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

    // Emitted with the new value on every user-driven change (drag or
    // track tap), not on programmatic `value` assignments.
    signal moved(real value)

    implicitWidth: 200
    implicitHeight: 40

    Accessible.role: Accessible.Slider
    Accessible.name: qsTr("Slider")

    // Normalised 0..1 position of the current value. Guards a zero-width
    // range so a misconfigured from == to collapses to 0 instead of NaN.
    readonly property real _ratio: to > from ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0
    // Travel available to the handle centre: the full width minus the
    // handle so it never overhangs either end.
    readonly property real _trackWidth: width - handle.width

    // Map a pointer x (in root coordinates) to a clamped value and emit
    // moved() if it actually changed.
    function _setFromX(px) {
        if (_trackWidth <= 0)
            return;
        const r = Math.max(0, Math.min(1, (px - handle.width / 2) / _trackWidth));
        const v = from + r * (to - from);
        if (v !== value) {
            value = v;
            moved(v);
        }
    }

    readonly property color _disabledTint: Qt.rgba(Theme.on_surface.r, Theme.on_surface.g, Theme.on_surface.b, StateLayer.disabled_content)

    // Inactive track.
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        x: handle.width / 2
        width: root._trackWidth
        height: 4
        radius: 2
        color: root.enabled ? Theme.surface_variant : Qt.rgba(Theme.on_surface.r, Theme.on_surface.g, Theme.on_surface.b, StateLayer.disabled_container)
    }

    // Active (filled) track.
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        x: handle.width / 2
        width: root._trackWidth * root._ratio
        height: 4
        radius: 2
        color: root.enabled ? Theme.primary : root._disabledTint
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
