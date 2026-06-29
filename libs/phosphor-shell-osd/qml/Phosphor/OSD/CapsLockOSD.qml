// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.OSD.CapsLockOSD, the caps-lock on-screen display.
//
// An OSDCard with a caps-lock glyph and no progress bar. `active` is
// true when caps lock is on; the glyph fills with the primary accent and
// the label reflects the state.

import QtQuick
import QtQuick.Shapes
import Phosphor.Theme

OSDCard {
    id: osd

    // True when caps lock is on, set by OSDHost.
    property bool active: false
    // Unused here; present so OSDHost can set it generically.
    property real value: 0

    label: osd.active ? qsTr("Caps Lock on") : qsTr("Caps Lock off")
    showProgress: false

    // Glyph tint: caps-on lights the glyph with the primary accent.
    readonly property color glyphColor: osd.active ? Theme.primary : Theme.on_surface

    icon: Component {
        Item {
            implicitWidth: 30
            implicitHeight: 30

            // Upward arrow.
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    fillColor: osd.glyphColor
                    strokeColor: "transparent"

                    PathSvg {
                        path: "M 15 3 L 25 14 H 20 V 20 H 10 V 14 H 5 Z"
                    }
                }
            }
            // Base bar under the arrow.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 24
                width: 14
                height: 3
                radius: 1.5
                color: osd.glyphColor
            }
        }
    }
}
