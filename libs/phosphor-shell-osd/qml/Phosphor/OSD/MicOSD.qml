// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.OSD.MicOSD, the microphone-mute on-screen display.
//
// An OSDCard with a microphone glyph and no progress bar. `active` is
// true when the mic is muted; the glyph then shows a red slash and the
// label reflects the state.

import QtQuick
import QtQuick.Shapes
import Phosphor.Theme

OSDCard {
    id: osd

    // True when the microphone is muted, set by OSDHost.
    property bool active: false
    // Unused here; present so OSDHost can set it generically.
    property real value: 0

    label: osd.active ? qsTr("Microphone muted") : qsTr("Microphone on")
    showProgress: false

    // Glyph tint: muted dims the mic body (the red slash carries the state).
    readonly property color glyphColor: osd.active ? Theme.on_surface_variant : Theme.on_surface

    icon: Component {
        Item {
            implicitWidth: 30
            implicitHeight: 30

            // Capsule.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 3
                width: 11
                height: 16
                radius: width / 2
                color: osd.glyphColor
            }
            // Stand stem.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 21
                width: 2
                height: 4
                color: osd.glyphColor
            }
            // Base.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 25
                width: 13
                height: 2
                radius: 1
                color: osd.glyphColor
            }
            // Mute slash.
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    fillColor: "transparent"
                    strokeColor: osd.active ? Theme.error : "transparent"
                    strokeWidth: 2.5
                    capStyle: ShapePath.RoundCap

                    PathSvg {
                        path: "M 5 5 L 25 25"
                    }
                }
            }
        }
    }
}
