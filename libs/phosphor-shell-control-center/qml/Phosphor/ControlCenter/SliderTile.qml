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
    // Same contract as Tile: the tile supplies the detail view, and the
    // chevron appears only when there is one behind it.
    property string detailTitle: ""
    property Component detailContent: null
    property bool detailEnabled: true
    readonly property bool hasDetail: root.detailEnabled && root.detailContent !== null
    // Layout hint read by ControlCenter: this tile wants the full grid
    // width. A slider in a half-width cell is too short to aim at, and the
    // label + readout row needs the room. The host applies it as a column
    // span, so the tile does not have to know how many columns there are.
    readonly property bool spansRow: true

    // The user moved the slider. Carries the new value in [from, to].
    signal moved(real value)
    // The user activated the icon, which is the mute affordance on tiles
    // that have one. Hosts without a mute concept leave it unconnected AND
    // leave `hasIconAction` false, so the icon is presented as decoration
    // rather than as a button that does nothing when pressed.
    signal iconActivated
    // Whether the icon is an affordance at all. Brightness has no mute, so
    // announcing its icon as a Mute button would tell a screen-reader user
    // about a control that is not there.
    property bool hasIconAction: false
    // The user asked for this tile's detail view.
    signal detailRequested

    function _activateIcon(): void {
        if (root.available && root.hasIconAction)
            root.iconActivated();
    }

    function _activateIconFromKey(event: var): void {
        // Autorepeat guard: a held key must not toggle mute twice. Same
        // shape as Tile's own key activation.
        if (event.isAutoRepeat)
            return;
        root._activateIcon();
        event.accepted = true;
    }

    // Width is a floor, not a target: the host stretches tiles to their
    // cell. Kept small enough that two half-width toggle cells, not this,
    // set the grid's natural width.
    implicitWidth: 160
    // A FLOOR, not a fixed size. 72 is the design height, but the label and
    // readout are text: at a larger font scale the content outgrows it and
    // the tile would clip, taking the grid's row height with it.
    implicitHeight: Math.max(72, content.implicitHeight + 2 * Tokens.spacing_m)
    opacity: root.available ? 1 : StateLayer.disabled_content

    readonly property string _readout: {
        if (root.readout !== "")
            return root.readout;
        if (root.to <= root.from)
            return "";
        return Math.round(100 * (root.value - root.from) / (root.to - root.from)) + "%";
    }

    // A GROUPING, not a Slider. The real slider is a child of this item and
    // carries the Slider role itself, so declaring one here too announced
    // two nested sliders, with the outer one exposing no value and no
    // increase/decrease action. The tile is the container; the control
    // inside it is the control.
    Accessible.role: Accessible.Grouping
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
            id: content

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

                    // Announced as a button only when it is one. A tile with
                    // no mute concept leaves hasIconAction false, and the icon
                    // then reads as part of the tile rather than as a separate
                    // control that does nothing.
                    Accessible.role: root.hasIconAction ? Accessible.Button : Accessible.Graphic
                    Accessible.ignored: !root.hasIconAction
                    Accessible.name: root.muted ? qsTr("Unmute") : qsTr("Mute")
                    Accessible.onPressAction: root._activateIcon()

                    // Reachable by keyboard, like DetailPanel's back button
                    // and Tile's own surface. A control with a Button role
                    // that only pointers can reach is not usable.
                    activeFocusOnTab: root.available && root.hasIconAction
                    Keys.onSpacePressed: event => root._activateIconFromKey(event)
                    Keys.onReturnPressed: event => root._activateIconFromKey(event)
                    Keys.onEnterPressed: event => root._activateIconFromKey(event)

                    Kirigami.Icon {
                        anchors.fill: parent
                        source: root.iconName
                        color: surface.contentColor
                    }

                    HoverHandler {
                        enabled: root.available && root.hasIconAction
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        enabled: root.available && root.hasIconAction
                        onTapped: root._activateIcon()
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
                    opacity: StateLayer.secondary_content
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
                // Named for what it controls. Without this the slider
                // announced only its own value, leaving a screen-reader user
                // to infer from the grouping which control they had reached.
                Accessible.name: root.label
                Accessible.description: root._readout
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
