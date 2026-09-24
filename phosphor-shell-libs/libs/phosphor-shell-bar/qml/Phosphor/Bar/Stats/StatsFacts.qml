// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme

Column {
    id: root
    property var entries: []
    width: parent ? parent.width : 0
    spacing: 13
    Rectangle {
        width: parent.width
        height: 1
        color: Appearance.outline
    }
    RowLayout {
        width: parent.width
        spacing: 12
        Repeater {
            model: root.entries
            ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 6
                DetailText {
                    Layout.fillWidth: true
                    text: modelData[0]
                    muted: true
                    size: 9
                }
                DetailText {
                    Layout.fillWidth: true
                    text: modelData[1]
                    size: 13
                }
            }
        }
    }
    Rectangle {
        width: parent.width
        height: 1
        color: Appearance.outline
    }
}
