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

            // Raw hex rather than Phosphor.Theme tokens, unlike the other
            // demos. Deliberate: this one demonstrates PerScreen's delegate
            // lifecycle, and importing Phosphor.Theme would make it link the
            // theme library and its QML plugin to paint two labels. The
            // colors are chrome for a diagnostic window, not a palette
            // example, so there is nothing here for a token to carry.
            color: "#202028"

            Text {
                anchors.centerIn: parent
                color: "#ffffff"
                font.pixelSize: 20
                text: screenWindow.name + (screenWindow.isPrimary ? qsTr("  [PRIMARY]") : "")
            }

            Text {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.margins: 12
                color: "#a0a0c0"
                font.pixelSize: 12
                text: qsTr("index %1").arg(screenWindow.index)
            }
        }
    }
}
