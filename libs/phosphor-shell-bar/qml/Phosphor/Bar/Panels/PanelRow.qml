// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.PanelRow, one selectable line in a bar panel.
//
// A network, a Bluetooth device, an audio stream, a battery: every panel
// list is the same line — a leading glyph, a label with an optional second
// line, an optional trailing readout, and a press. Shared so the six
// panels agree on the row height, the hover treatment and what assistive
// tech is told, rather than each inventing its own.
//
// `current` marks the row that is already the case (the connected network,
// the playing player). It is a state, not a selection: the row stays
// pressable so a press can disconnect it.

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
    /// Whether this row is the active one. Lights the label and tints the
    /// leading glyph.
    property bool current: false
    /// Whether pressing does anything. A false value keeps the row legible
    /// but drops the cursor, the hover layer and the press.
    property bool pressable: true
    /// Announced to assistive tech as the action. Falls back to the label.
    property string actionName: ""

    signal clicked

    // Rows sit in a Column that sets their width, so only the height is
    // this type's to decide. The floor keeps a one-line row a comfortable
    // pointer target rather than the bare height of its text.
    implicitHeight: Math.max(36, layout.implicitHeight + Tokens.spacing_s * 2)

    Accessible.role: root.pressable ? Accessible.Button : Accessible.StaticText
    Accessible.name: {
        const base = root.actionName !== "" ? root.actionName : root.label;
        if (root.sublabel === "")
            return base;
        return base + ", " + root.sublabel;
    }
    Accessible.onPressAction: root._activate()

    function _activate() {
        if (root.pressable)
            root.clicked();
    }

    // The hover state layer. Rounded to the row radius so it reads as one
    // line in the list rather than a full-bleed band.
    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_edge
        color: Theme.on_surface
        opacity: hover.hovered && root.pressable ? StateLayer.hover : 0

        Behavior on opacity {
            NumberAnimation {
                duration: hover.hovered ? Motion.duration_enter : Motion.duration_release
                easing: hover.hovered ? Motion.enter : Motion.release
            }
        }
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

    RowLayout {
        id: layout

        anchors.fill: parent
        anchors.leftMargin: Tokens.spacing_s
        anchors.rightMargin: Tokens.spacing_s
        spacing: Tokens.spacing_s

        Kirigami.Icon {
            visible: root.iconName !== ""
            // Layout.preferredWidth rather than width: the layout owns an
            // item's geometry, and a plain width binding here is overwritten
            // on the next layout pass.
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            source: root.iconName
            isMask: true
            color: root.current ? Theme.primary : Theme.on_surface_variant
            scale: tap.pressed ? 0.94 : 1

            Behavior on scale {
                NumberAnimation {
                    duration: Motion.duration_tick
                    easing: Motion.reveal
                }
            }
        }

        ColumnLayout {
            // fillWidth is what makes the labels elide instead of pushing
            // the trailing readout off the card: the text items take
            // whatever the glyph and the readout leave, and no more.
            Layout.fillWidth: true
            spacing: 0

            Text {
                Accessible.ignored: true
                Layout.fillWidth: true
                text: root.label
                color: root.current ? Theme.on_surface : Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_body_m
                font.family: Tokens.font_family_ui
                font.weight: root.current ? Tokens.font_weight_medium : Tokens.font_weight_regular
                elide: Text.ElideRight
            }

            Text {
                Accessible.ignored: true
                visible: root.sublabel !== ""
                Layout.fillWidth: true
                text: root.sublabel
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_label_s
                font.family: Tokens.font_family_ui
                elide: Text.ElideRight
            }
        }

        Text {
            Accessible.ignored: true
            visible: root.trailingText !== ""
            text: root.trailingText
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_label_s
            font.family: Tokens.font_family
        }
    }
}
