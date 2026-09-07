// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.VolumeRow, one PipeWire node's mute toggle and slider.
//
// The output, the input and each application stream are the same control,
// so they are one type. The glyph mutes, the slider sets every channel of
// the node together.
//
// The slider is driven by `moved`, not by a two-way binding on `value`.
// PipeWire's write is asynchronous: the node's volume only moves when the
// daemon echoes the new props back. A binding in both directions would let
// that echo fight the handle mid-drag and make it stutter backwards, so
// the binding is one way (node to slider) and the gesture is one way
// (slider to node).

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.PipeWire

Item {
    id: root

    /// The node this row controls. Null leaves the row inert rather than
    /// hidden, so a momentarily unresolved default sink does not make the
    /// panel jump.
    property PwNode node: null
    property string iconName: "audio-volume-high"
    property string label: ""
    property string sublabel: ""

    readonly property bool _muted: root.node ? root.node.muted : false
    readonly property real _volume: root.node && root.node.volumes.length > 0 ? root.node.volumes[0] : 0
    readonly property int _percent: Math.round(root._volume * 100)

    implicitHeight: layout.implicitHeight + Tokens.spacing_xs * 2

    ColumnLayout {
        id: layout

        anchors.fill: parent
        anchors.topMargin: Tokens.spacing_xs
        anchors.bottomMargin: Tokens.spacing_xs
        spacing: Tokens.spacing_xxs

        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.spacing_s

            Kirigami.Icon {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                source: root.iconName
                isMask: true
                color: root._muted ? Theme.on_surface_variant : Theme.on_surface
                opacity: muteHover.hovered ? 1 : 0.85

                HoverHandler {
                    id: muteHover

                    enabled: root.node !== null
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    enabled: root.node !== null
                    onTapped: {
                        if (root.node)
                            root.node.setMuted(!root._muted);
                    }
                }

                Accessible.role: Accessible.Button
                Accessible.name: root._muted ? qsTr("Unmute %1").arg(root.label) : qsTr("Mute %1").arg(root.label)
                Accessible.onPressAction: {
                    if (root.node)
                        root.node.setMuted(!root._muted);
                }
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
                text: root._percent + "%"
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_label_s
            }
        }

        PhosphorSlider {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            enabled: root.node !== null && !root._muted
            from: 0
            to: 1
            // Amplitude, not percent, because that is the unit PwNode takes.
            // 5% steps, matching the bar chip's scroll, so the two paths to
            // the same value do not disagree about granularity.
            stepSize: 0.05
            value: root._volume

            onMoved: v => {
                if (root.node)
                    // Clamped here: PwNode forwards the value to PipeWire
                    // verbatim, and the linear-amplitude contract does not
                    // include negatives.
                    root.node.setVolume(Math.max(0, Math.min(1, v)));
            }
        }
    }
}
