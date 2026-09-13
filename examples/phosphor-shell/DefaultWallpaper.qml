// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Phosphor.Theme

Image {
    source: "wallpapers/" + Appearance.settings.palette + ".png"
    fillMode: Image.PreserveAspectCrop
    asynchronous: true
    smooth: true
    Column {
        x: 42
        y: Appearance.bottom ? 38 : parent.height - height - 35
        spacing: 8
        Text {
            text: "PHOSPHOR"
            color: "#668da2c1"
            font.family: Tokens.font_family_ui
            font.pixelSize: 11
            font.letterSpacing: 4
        }
        Text {
            text: qsTr("your windows, in place")
            color: "#668da2c1"
            font.family: Tokens.font_family_ui
            font.pixelSize: 9
            font.letterSpacing: 1
        }
    }
}
