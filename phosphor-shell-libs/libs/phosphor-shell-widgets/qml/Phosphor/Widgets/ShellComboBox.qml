// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import Phosphor.Theme

Basic.ComboBox {
    id: root
    property int labelSize: 11
    implicitHeight: 30
    implicitWidth: Math.max(90, contentItem.implicitWidth + 38)
    leftPadding: 10
    rightPadding: 26
    font.family: Tokens.font_family_ui
    font.pixelSize: Math.round(labelSize * Appearance.textScale)
    background: Rectangle {
        radius: 7
        color: Appearance.card
        border.width: 1
        border.color: root.visualFocus ? Appearance.text : Appearance.outline
    }
    contentItem: Text {
        text: root.displayText
        color: Appearance.text
        font: root.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: root.width - width - 9
        y: (root.height - height) / 2
        text: "⌄"
        font.pixelSize: 12
        color: Appearance.muted
    }
    delegate: Basic.ItemDelegate {
        required property int index
        required property var modelData
        width: root.width
        implicitHeight: 32
        highlighted: root.highlightedIndex === index
        contentItem: Text {
            text: root.textRole ? modelData[root.textRole] : modelData
            color: Appearance.text
            font: root.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 5
            color: parent.highlighted ? Appearance.card : Appearance.recess
        }
    }
    popup: Basic.Popup {
        y: root.height + 4
        width: root.width
        padding: 4
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 260)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            Basic.ScrollIndicator.vertical: Basic.ScrollIndicator {}
        }
        background: Rectangle {
            radius: 8
            color: Appearance.recess
            border.width: 1
            border.color: Appearance.outline
        }
    }
}
