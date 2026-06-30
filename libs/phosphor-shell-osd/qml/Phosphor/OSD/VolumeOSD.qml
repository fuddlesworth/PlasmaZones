// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.OSD.VolumeOSD, the volume on-screen display.
//
// An OSDCard with a speaker glyph and a progress bar. `value` is the
// volume percentage (0..100); at 0 the speaker shows a muted cross.

import QtQuick
import QtQuick.Shapes
import Phosphor.Theme

OSDCard {
    id: osd

    // Volume percentage 0..100, set by OSDHost.
    property real value: 0
    // Unused here; present so OSDHost can set it generically.
    property bool active: false

    label: qsTr("Volume")
    showProgress: true
    progress: value / 100

    icon: Component {
        Item {
            implicitWidth: 30
            implicitHeight: 30

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                // Speaker body (cone + box).
                ShapePath {
                    fillColor: Theme.on_surface
                    strokeColor: "transparent"

                    PathSvg {
                        path: "M 4 11 H 9 L 16 5 V 25 L 9 19 H 4 Z"
                    }
                }
                // Sound wave, hidden when muted.
                ShapePath {
                    fillColor: "transparent"
                    strokeColor: osd.value > 0 ? Theme.on_surface : "transparent"
                    strokeWidth: 2
                    capStyle: ShapePath.RoundCap

                    PathSvg {
                        path: "M 19 9 C 23 13, 23 17, 19 21"
                    }
                }
                // Muted cross, shown only at zero.
                ShapePath {
                    fillColor: "transparent"
                    strokeColor: osd.value <= 0 ? Theme.error : "transparent"
                    strokeWidth: 2
                    capStyle: ShapePath.RoundCap

                    PathSvg {
                        path: "M 20 11 L 26 19 M 26 11 L 20 19"
                    }
                }
            }
        }
    }
}
