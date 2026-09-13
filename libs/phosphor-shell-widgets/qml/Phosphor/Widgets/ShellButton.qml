// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import Phosphor.Theme

Button {
    id: root
    property string iconName: ""
    property string label: text
    implicitWidth: text.length ? Math.max(36, contentItem.implicitWidth + 24) : 36
    implicitHeight: 36
    Accessible.name: label
    opacity: enabled ? 1 : 0.4
    background: Rectangle {
        radius: Math.min(Appearance.radius, height / 2)
        color: root.highlighted || root.down ? Qt.alpha(Appearance.accent, 0.28) : root.hovered ? Qt.alpha(Appearance.text, 0.12) : root.flat ? "transparent" : Appearance.card
        border.width: root.flat && !root.visualFocus ? 0 : 1
        border.color: root.visualFocus ? Appearance.text : Appearance.outline
    }
    contentItem: Item {
        implicitWidth: labelRow.implicitWidth
        implicitHeight: 18
        Row {
            id: labelRow
            anchors.centerIn: parent
            spacing: root.iconName !== "" && root.text !== "" ? 6 : 0
            Kirigami.Icon {
                visible: root.iconName !== ""
                width: visible ? 16 : 0
                height: 16
                anchors.verticalCenter: parent.verticalCenter
                source: root.iconName
                isMask: true
                color: Appearance.text
            }
            Text {
                text: root.text
                anchors.verticalCenter: parent.verticalCenter
                color: Appearance.text
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_body_s
            }
        }
    }
}
