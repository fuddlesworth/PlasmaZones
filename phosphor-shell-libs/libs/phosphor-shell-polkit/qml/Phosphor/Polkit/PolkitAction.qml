// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic
import Phosphor.Theme

Button {
    id: root
    property bool primary: false
    implicitWidth: Math.max(primary ? 126 : 68, contentItem.implicitWidth + 30)
    implicitHeight: Math.max(40, contentItem.implicitHeight + 16)
    padding: 8
    horizontalPadding: 15
    opacity: enabled ? 1 : .4
    Keys.onReturnPressed: event => {
        if (enabled && !event.isAutoRepeat)
            clicked();
    }
    Keys.onEnterPressed: event => {
        if (enabled && !event.isAutoRepeat)
            clicked();
    }
    background: Rectangle {
        radius: Appearance.radius * .4
        color: root.primary ? Qt.tint(Appearance.card, Qt.alpha(Appearance.accent, root.hovered ? .38 : .24)) : Qt.alpha(Appearance.card, root.hovered ? 1 : .35)
        border.width: 1
        border.color: root.visualFocus ? Appearance.text : root.primary ? Qt.tint(Appearance.outline, Qt.alpha(Appearance.accent, .46)) : Appearance.outline
    }
    contentItem: Text {
        text: root.text
        textFormat: Text.PlainText
        color: Appearance.text
        font.family: Tokens.font_family_ui
        font.pixelSize: Math.round(12 * Appearance.textScale)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }
}
