// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.PanelRow, one selectable line in a bar transient.
//
// A network, a Bluetooth device, a battery: every panel list is the same
// line — a leading glyph, a label with an optional second line, an
// optional trailing readout, and a 2px underline carrying the row's
// STATE on the state axis (cyan resting, blue active, purple pending,
// rose at-limit).
//
// The underline is also what marks hover, by going to full opacity. The
// first cut used a rounded translucent rectangle behind the row, which is
// a filled state layer and exactly what R1 forbids: on a Phosphor surface
// colour lives in strokes, bands and underlines, and depth is a stroke,
// never a fill.
//
// `current` marks the row that is already the case (the connected
// network, the playing player). It is a state, not a selection: the row
// stays pressable so a press can undo it.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme

Item {
    id: root

    property string iconName: ""
    property string label: ""
    property string sublabel: ""
    /// Short readout on the trailing edge (a percentage, a signal strength).
    property string trailingText: ""
    /// Whether this row is the active one.
    property bool current: false
    /// Whether pressing does anything.
    property bool pressable: true
    /// Announced to assistive tech as the action. Falls back to the label.
    property string actionName: ""
    /// Where this row sits on the state axis: 0 resting, 0.33 active,
    /// 0.67 pending, 1 at-limit. `current` overrides to active.
    property real railT: 0

    signal clicked

    // Rows sit in a Column that sets their width, so only the height is
    // this type's to decide. The floor keeps a one-line row a comfortable
    // pointer target rather than the bare height of its text.
    implicitHeight: Math.max(44, layout.implicitHeight + Tokens.spacing_s * 2)

    Accessible.role: root.pressable ? Accessible.Button : Accessible.StaticText
    Accessible.name: {
        const base = root.actionName !== "" ? root.actionName : root.label;
        return root.sublabel === "" ? base : base + ", " + root.sublabel;
    }
    Accessible.onPressAction: root._activate()

    function _activate() {
        if (root.pressable)
            root.clicked();
    }

    HoverHandler {
        id: hover

        enabled: root.pressable
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap

        enabled: root.pressable
        onTapped: root._activate()
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
                visible: root.iconName !== ""
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                source: root.iconName
                isMask: true
                color: Theme.on_surface
                // Brightness carries state here too, matching the chips.
                opacity: root.current ? 1 : (hover.hovered ? 0.9 : 0.55)
                scale: tap.pressed ? 0.94 : 1

                Behavior on scale {
                    NumberAnimation {
                        duration: Motion.duration_tick
                        easing: Motion.reveal
                    }
                }
            }

            ColumnLayout {
                // fillWidth is what makes the labels elide instead of
                // pushing the trailing readout off the panel.
                Layout.fillWidth: true
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    text: root.label
                    color: root.current ? Theme.on_surface : Theme.on_surface_variant
                    font.pixelSize: Tokens.font_size_body_m
                    font.family: Tokens.font_family_ui
                    font.weight: root.current ? Tokens.font_weight_medium : Tokens.font_weight_regular
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

            Text {
                visible: root.trailingText !== ""
                text: root.trailingText
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_label_s
                font.family: Tokens.font_family
            }
        }

        // The state underline. Dim when the row is just present, full when
        // it is the case or under the pointer: one device for separation,
        // state and hover.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 2
            color: root.current ? Spectrum.at(0.33) : Spectrum.at(root.railT)
            opacity: root.current ? 1 : (hover.hovered ? 0.8 : 0.18)

            Behavior on opacity {
                NumberAnimation {
                    duration: hover.hovered ? Motion.duration_enter : Motion.duration_release
                    easing: hover.hovered ? Motion.enter : Motion.release
                }
            }
        }
    }
}
