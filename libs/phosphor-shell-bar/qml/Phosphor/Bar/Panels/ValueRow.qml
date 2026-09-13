// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.ValueRow, one row whose UNDERLINE is its control.
//
// The 2px line under the label is not decoration beside a slider: it is
// the slider. Length is the value, hue is the state axis, and dragging
// anywhere along the row sets it. That is the same device as the rail,
// the chip underline and the tether, which is what makes the shell read
// as one system rather than a themed widget set (05 R1: colour lives in
// strokes, bands and underlines, never in fills).
//
// This replaced a PhosphorSlider with a track and a handle. The handle is
// the more discoverable control and that is a real cost, taken knowingly:
// the row is a 44px target, the whole of it drags, and one filled track
// would have been the only M3 fill on any Phosphor surface.
//
// Motion is R6: the fill retargets from wherever it currently is rather
// than restarting, so an external change (a media key, another client)
// slides from the old value instead of jumping. While the pointer is
// down the binding is released, or the daemon's echo would fight the
// drag and stutter the line backwards under the finger.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    property string iconName: ""
    property string label: ""
    property string sublabel: ""

    /// Current value, 0..1. Bound from the service; written back through
    /// `moved` rather than two-way, so an async echo cannot fight a drag.
    property real value: 0
    /// Whether the row can be dragged at all.
    property bool adjustable: true
    /// Whether the leading glyph toggles something (mute). False leaves
    /// the glyph as a plain indicator.
    property bool togglable: false
    property bool toggled: false
    /// Hue for the fill, as a point on the state axis.
    property real railT: 0.4

    /// The user moved the value to `v` (0..1). Fired continuously during a
    /// drag; the caller decides whether to throttle.
    signal moved(real v)
    /// The leading glyph was pressed.
    signal toggledRequested

    readonly property int percent: Math.round(root.value * 100)
    // While dragging, show the finger's value; otherwise follow the
    // service. Two sources, one at a time, never both.
    readonly property real _shown: drag.active ? drag.pending : root.value

    implicitHeight: Math.max(44, layout.implicitHeight + Tokens.spacing_s * 2)

    Accessible.role: root.adjustable ? Accessible.Slider : Accessible.StaticText
    // The percentage is folded into the NAME rather than set through an
    // Accessible.value: the attached property has no such member, and the
    // value interface a screen reader would read comes from QAccessible's
    // value interface, which only the QQC2 controls implement. A plain
    // Item cannot supply one, so the number has to be spoken in the name
    // or it is not spoken at all.
    Accessible.name: root.toggled ? qsTr("%1, muted").arg(root.label) : qsTr("%1, %2 percent").arg(root.label).arg(root.percent)
    Accessible.onIncreaseAction: root._nudge(1)
    Accessible.onDecreaseAction: root._nudge(-1)

    function _nudge(steps) {
        if (!root.adjustable)
            return;
        root.moved(Math.max(0, Math.min(1, root.value + steps * 0.05)));
    }

    ColumnLayout {
        id: layout

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Tokens.spacing_xs

        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.spacing_s

            Kirigami.Icon {
                id: glyph

                visible: root.iconName !== ""
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                source: root.iconName
                isMask: true
                // Brightness is the state, exactly as it is on the bar's
                // own chips: no colour change, no filled background.
                color: Theme.on_surface
                opacity: root.toggled ? 0.35 : (glyphHover.hovered ? 1 : 0.9)

                Behavior on opacity {
                    NumberAnimation {
                        duration: glyphHover.hovered ? Motion.duration_enter : Motion.duration_release
                        easing: glyphHover.hovered ? Motion.enter : Motion.release
                    }
                }

                HoverHandler {
                    id: glyphHover

                    enabled: root.togglable
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    enabled: root.togglable
                    onTapped: root.toggledRequested()
                }

                Accessible.role: Accessible.Button
                Accessible.name: root.toggled ? qsTr("Unmute %1").arg(root.label) : qsTr("Mute %1").arg(root.label)
                Accessible.onPressAction: root.toggledRequested()
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    text: root.label
                    color: Theme.on_surface
                    font.pixelSize: Tokens.font_size_body_m
                    font.family: Tokens.font_family_ui
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.sublabel !== ""
                    text: root.sublabel
                    color: Theme.on_surface_variant
                    font.pixelSize: Tokens.font_size_label_s
                    font.family: Tokens.font_family_ui
                    elide: Text.ElideRight
                }
            }

            TabularText {
                text: root.toggled ? qsTr("muted") : root.percent + "%"
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_label_s
                // R7: the changed digit takes a 2px spectrum underline that
                // enters and releases. Free here, since the atom does it.
                tickOnChange: true
                t: root.railT
            }
        }

        // The control. Full row width, 2px, unfilled remainder at a low
        // white so the extent is legible without becoming a track.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 2

            Rectangle {
                anchors.fill: parent
                color: Theme.on_surface
                opacity: 0.10
            }

            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, root._shown))
                height: parent.height
                color: Spectrum.at(root.railT)
                opacity: root.toggled ? 0.35 : 1

                // Retarget, never restart (R6). Suppressed mid-drag so the
                // line tracks the finger exactly.
                Behavior on width {
                    enabled: !drag.active
                    NumberAnimation {
                        duration: Motion.duration_short_4
                        easing: Motion.standard
                    }
                }
            }
        }
    }

    // The whole row drags, which is what buys back the affordance the
    // handle would have given: the target is the row, not the 2px line.
    DragHandler {
        id: drag

        property real pending: root.value

        enabled: root.adjustable
        target: null
        xAxis.enabled: true
        yAxis.enabled: false

        onCentroidChanged: {
            if (!active)
                return;
            drag.pending = Math.max(0, Math.min(1, centroid.position.x / root.width));
            root.moved(drag.pending);
        }
    }

    TapHandler {
        enabled: root.adjustable
        onTapped: eventPoint => {
            root.moved(Math.max(0, Math.min(1, eventPoint.position.x / root.width)));
        }
    }

    HoverHandler {
        enabled: root.adjustable
        cursorShape: Qt.PointingHandCursor
    }
}
