// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

Rectangle {
    default property alias content: rows.data
    width: parent ? parent.width : 0
    implicitHeight: rows.implicitHeight + 2
    radius: Appearance.radius * 0.64
    color: Qt.alpha(Appearance.recess, 0.32)
    border.width: 1
    border.color: Appearance.outline
    Column {
        id: rows
        x: 1
        y: 1
        width: parent.width - 2
        spacing: 1
    }
}
