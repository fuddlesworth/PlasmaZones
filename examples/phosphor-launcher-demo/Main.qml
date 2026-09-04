// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-launcher-demo, the Phase 4.2 acceptance demo.
//
// The spotlight launcher over this machine's real applications,
// clipboard history and PATH: Enter on an app row launches it. Escape
// and a successful activation both reset the surface rather than quit,
// so the next search can start at once.
//
// The backdrop is a darkened brand gradient rather than flat navy, for
// the reason the bar-canvas demo documents: a flat surface-coloured
// backdrop is near-identical to surface_container and makes the
// launcher card read as a hole. A real shell sits on a wallpaper.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Launcher

Window {
    id: window

    width: 900
    height: 640
    visible: true
    title: qsTr("Phosphor Launcher")
    color: "#050916"

    Rectangle {
        anchors.fill: parent

        gradient: Gradient {
            GradientStop {
                position: 0
                color: Qt.darker(Theme.brand_stop_0, 1.6)
            }

            GradientStop {
                position: 0.55
                color: Qt.darker(Theme.brand_stop_1, 1.4)
            }

            GradientStop {
                position: 1
                color: Qt.darker(Theme.brand_stop_2, 1.25)
            }
        }

        Launcher {
            id: launcher

            anchors.horizontalCenter: parent.horizontalCenter
            y: Tokens.spacing_xxxl * 2
            width: 640
            results: LauncherResults

            Component.onCompleted: reset()
            onActivated: reset()
            onDismissed: reset()
        }

        Text {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: Tokens.spacing_l
            text: qsTr("Type to search. These are your real apps, clipboard and PATH, and Enter launches.")
            color: Theme.on_surface_variant
            font.family: Tokens.font_family
            font.pixelSize: Tokens.font_size_body_s
        }
    }
}
