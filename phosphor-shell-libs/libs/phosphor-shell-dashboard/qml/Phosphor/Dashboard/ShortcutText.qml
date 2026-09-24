// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

Text {
    property real size: 11
    property bool muted: false
    property bool kicker: false
    color: muted || kicker ? Appearance.muted : Appearance.text
    font.family: kicker ? Tokens.font_family_mono : Tokens.font_family_ui
    font.pixelSize: Math.round(size * Appearance.textScale)
    font.letterSpacing: kicker ? 1.5 : 0
    textFormat: Text.PlainText
    wrapMode: Text.Wrap
}
