// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

Text {
    property int size: 12
    property bool muted: false
    color: muted ? Appearance.muted : Appearance.text
    font.family: Tokens.font_family_ui
    font.pixelSize: Math.round(size * Appearance.textScale)
    wrapMode: Text.Wrap
    textFormat: Text.PlainText
}
