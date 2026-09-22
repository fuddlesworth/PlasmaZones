// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-perscreen-demo Main.qml. PerScreen with a Window delegate;
// one small floating window per monitor. Hot-plug a display → window
// appears; unplug → window disappears.

import Phosphor.Shell
import QtQuick

PerScreen {
    model: screensModel

    delegate: Component {
        Window {
            id: screenWindow

            required property var phosphorScreen
            required property string name
            required property int index
            required property bool isPrimary

            // Position the window at (40, 40) relative to its target
            // screen's virtual-desktop origin. The QML `Window.screen`
            // property is read-only QQuickScreenInfo (NOT QScreen*),
            // so we can't directly setScreen() from QML — but x/y in
            // virtual-desktop coordinates put the window over the
            // right monitor on X11, and on Wayland the compositor
            // routes the surface based on its position. The delegate
            // lifecycle (one window per screen, identity preserved
            // across hot-plug) is independent of which monitor the
            // compositor ultimately renders it on.
            x: screenWindow.phosphorScreen.geometry.x + 40
            y: screenWindow.phosphorScreen.geometry.y + 40
            width: 320
            height: 200
            visible: true
            title: screenWindow.name
            color: "#202028"

            Text {
                anchors.centerIn: parent
                color: "#ffffff"
                font.pixelSize: 20
                text: screenWindow.name + (screenWindow.isPrimary ? "  [PRIMARY]" : "")
            }

            Text {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.margins: 12
                color: "#a0a0c0"
                font.pixelSize: 12
                text: "index " + screenWindow.index
            }
        }
    }
}
