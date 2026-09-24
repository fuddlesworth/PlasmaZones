// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme

Rectangle {
    id: root
    default property alias contents: body.data
    property alias body: body
    implicitHeight: body.implicitHeight + 32
    radius: Math.max(8, Appearance.radius * 0.7)
    color: Qt.alpha(Appearance.card, 0.28)
    border.color: Appearance.outline
    ColumnLayout {
        id: body
        x: 16
        y: 16
        width: parent.width - 32
        spacing: 14
    }
}
