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
//       label: i18n("Volume")
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
    implicitHeight: column.implicitHeight + 40

    Rectangle {
        anchors.fill: parent
        radius: 20
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
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        spacing: 14

        Loader {
            Layout.alignment: Qt.AlignHCenter
            sourceComponent: card.icon
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: card.label
            color: Theme.on_surface
            font.pixelSize: 15
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        // Progress track + fill. Reserved out of layout when not shown so
        // a stateful OSD (mic/caps) is a compact icon+label card.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 6
            visible: card.showProgress
            radius: 3
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
