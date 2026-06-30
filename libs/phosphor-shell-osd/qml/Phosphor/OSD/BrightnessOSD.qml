// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.OSD.BrightnessOSD, the brightness on-screen display.
//
// An OSDCard with a sun glyph and a progress bar. `value` is the
// brightness percentage (0..100).

import QtQuick
import Phosphor.Theme

OSDCard {
    id: osd

    // Brightness percentage 0..100, set by OSDHost.
    property real value: 0
    // Unused here; present so OSDHost can set it generically.
    property bool active: false

    label: qsTr("Brightness")
    showProgress: true
    progress: value / 100

    icon: Component {
        Item {
            implicitWidth: 30
            implicitHeight: 30

            // Sun disc.
            Rectangle {
                anchors.centerIn: parent
                width: 13
                height: 13
                radius: width / 2
                color: Theme.on_surface
            }
            // Eight rays around the disc.
            Repeater {
                model: 8

                Item {
                    required property int index

                    anchors.fill: parent
                    rotation: index * 45

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 0
                        width: 2.5
                        height: 5
                        radius: 1.25
                        color: Theme.on_surface
                    }
                }
            }
        }
    }
}
