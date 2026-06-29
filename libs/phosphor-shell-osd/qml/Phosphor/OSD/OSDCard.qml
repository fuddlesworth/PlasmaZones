// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.OSD.OSDCard, shared chrome for the built-in OSDs.
//
// A rounded elevated card holding a glyph, a label, and an optional
// progress bar. The four built-in OSDs (VolumeOSD, BrightnessOSD,
// MicOSD, CapsLockOSD) are each an OSDCard with their own glyph and
// bindings, so the card frame, elevation, spacing, and progress styling
// live in one place.
//
//   OSDCard {
//       label: qsTr("Volume")
//       showProgress: true
//       progress: value / 100
//       icon: Component { /* glyph */ }
//   }

import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: card

    // Glyph component shown at the top of the card.
    property Component icon: null
    property string label: ""
    // Whether to show the progress bar (volume/brightness yes; mic/caps no).
    property bool showProgress: false
    // Progress fraction 0..1.
    property real progress: 0

    implicitWidth: 240
    implicitHeight: column.implicitHeight + 2 * Tokens.spacing_l

    // An OSD is a transient status announcement, so expose it to assistive
    // tech as an alert. Fold the progress percentage into the name for the
    // OSDs that show it (volume/brightness); the stateful ones (mic/caps)
    // carry their state in the label already.
    Accessible.role: Accessible.AlertMessage
    Accessible.name: card.showProgress ? qsTr("%1, %2 percent").arg(card.label).arg(Math.round(Math.max(0, Math.min(1, card.progress)) * 100)) : card.label

    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_l
        color: Theme.surface_container_high
        layer.enabled: true
        layer.effect: ElevationShadow {
            level: 3
        }
    }

    ColumnLayout {
        id: column

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Tokens.spacing_xl
        anchors.rightMargin: Tokens.spacing_xl
        spacing: Tokens.spacing_m

        Loader {
            Layout.alignment: Qt.AlignHCenter
            sourceComponent: card.icon
        }

        Text {
            // The card root already announces the label (and percentage) as
            // its AlertMessage name, so keep this Text out of the a11y tree to
            // avoid a screen reader reading the label twice.
            Accessible.ignored: true
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: card.label
            color: Theme.on_surface
            font.pixelSize: Tokens.font_size_title_s
            font.weight: Tokens.font_weight_medium
            elide: Text.ElideRight
        }

        // Progress track + fill. Reserved out of layout when not shown so
        // a stateful OSD (mic/caps) is a compact icon+label card.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 6
            visible: card.showProgress
            radius: height / 2
            color: Theme.surface_variant

            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, card.progress))
                height: parent.height
                radius: parent.radius
                color: Theme.primary

                Behavior on width {
                    NumberAnimation {
                        duration: Motion.duration_short_3
                        easing: Motion.standard
                    }
                }
            }
        }
    }
}
