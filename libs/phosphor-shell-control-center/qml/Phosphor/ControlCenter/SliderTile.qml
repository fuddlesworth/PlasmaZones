// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.ControlCenter.SliderTile, the chrome for one continuous control.
//
// The companion to Tile: where Tile is a toggle with a readout, this is a
// control with a range — volume, brightness. Same surface treatment and
// the same detail-chevron affordance, but the tile face carries a slider
// instead of an on/off state, and it spans the grid rather than sitting
// in a single cell (a slider in a half-width cell is too short to aim at).
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
// `value` follows the service's echo. That matters more here than on a
// toggle, because a service that clamps or quantises a request (PipeWire
// rounding a volume curve, a backlight with 16 steps) would otherwise
// leave the handle somewhere the hardware never went.
//
// Kirigami.Icon draws its own fallback glyph for a name the icon theme
// cannot resolve, so a missing icon never yields an invisible control.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    // Icon glyph name, resolved by the host's icon theme.
    property string iconName: ""
    // What this tile controls ("Volume", "Brightness").
    property string label: ""
    // Current value, in [from, to]. Driven by the service.
    property real value: 0
    property real from: 0
    property real to: 100
    // Shown after the label. Empty derives a percentage from the range,
    // which is what both current consumers want.
    property string readout: ""
    // Drawn in the muted/disabled treatment. Distinct from `available`:
    // a muted sink is present and adjustable, it just is not audible.
    property bool muted: false
    // Set false when the underlying service is missing or the hardware is
    // absent. Inert but still visible, so the grid does not reflow as
    // services come and go.
    property bool available: true
    // Whether this tile has a detail view (a device picker, usually).
    property bool hasDetail: false
    // Layout hint read by ControlCenter: this tile wants the full grid
    // width. A slider in a half-width cell is too short to aim at, and the
    // label + readout row needs the room. The host applies it as a column
    // span, so the tile does not have to know how many columns there are.
    readonly property bool spansRow: true

    // The user moved the slider. Carries the new value in [from, to].
    signal moved(real value)
    // The user activated the icon, which is the mute affordance on tiles
    // that have one. Hosts without a mute concept leave it unconnected.
    signal iconActivated
    // The user asked for this tile's detail view.
    signal detailRequested

    // Width is a floor, not a target: the host stretches tiles to their
    // cell. Kept small enough that two half-width toggle cells, not this,
    // set the grid's natural width.
    implicitWidth: 160
    implicitHeight: 72
    opacity: root.available ? 1 : 0.38

    readonly property string _readout: {
        if (root.readout !== "")
            return root.readout;
        if (root.to <= root.from)
            return "";
        return Math.round(100 * (root.value - root.from) / (root.to - root.from)) + "%";
    }

    Accessible.role: Accessible.Slider
    Accessible.name: root.label
    Accessible.description: root._readout

    Rectangle {
        id: surface

        anchors.fill: parent
        radius: Tokens.radius_l
        // Always the container treatment, never the filled/primary one a
        // toggle uses for its on state: a range has no "on", and filling
        // the tile would fight the slider's own active track for meaning.
        color: Theme.surface_container_high
        border.width: 1
        border.color: Theme.outline_variant

        readonly property color contentColor: root.muted ? Theme.on_surface_variant : Theme.on_surface

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Tokens.spacing_m
            anchors.rightMargin: root.hasDetail ? Tokens.spacing_m + chevron.width + Tokens.spacing_s : Tokens.spacing_m
            spacing: Tokens.spacing_xxs

            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.spacing_s

                Item {
                    id: iconButton

                    implicitWidth: 22
                    implicitHeight: 22

                    Accessible.role: Accessible.Button
                    Accessible.name: root.muted ? qsTr("Unmute") : qsTr("Mute")
                    Accessible.onPressAction: root.iconActivated()

                    Kirigami.Icon {
                        anchors.fill: parent
                        source: root.iconName
                        color: surface.contentColor
                    }

                    HoverHandler {
                        enabled: root.available
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        enabled: root.available
                        onTapped: root.iconActivated()
                    }
                }

                Text {
                    Accessible.ignored: true
                    text: root.label
                    color: surface.contentColor
                    font.family: Tokens.font_family
                    font.pixelSize: Tokens.font_size_label_l
                    font.weight: Tokens.font_weight_medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    Accessible.ignored: true
                    text: root._readout
                    color: surface.contentColor
                    opacity: 0.75
                    font.family: Tokens.font_family
                    font.pixelSize: Tokens.font_size_body_s
                }
            }

            PhosphorSlider {
                id: slider

                enabled: root.available
                from: root.from
                to: root.to
                value: root.value
                Layout.fillWidth: true
                onMoved: v => root.moved(v)
            }
        }

        Kirigami.Icon {
            id: chevron

            anchors.right: parent.right
            anchors.rightMargin: Tokens.spacing_s
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            source: "go-next-symbolic"
            color: surface.contentColor
            visible: root.hasDetail

            HoverHandler {
                enabled: root.available && root.hasDetail
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                enabled: root.available && root.hasDetail
                onTapped: root.detailRequested()
            }
        }
    }
}
