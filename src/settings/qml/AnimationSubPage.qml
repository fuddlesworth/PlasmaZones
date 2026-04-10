// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    default property alias content: contentColumn.data

    contentHeight: contentColumn.implicitHeight
    clip: true

    ColumnLayout {
        id: contentColumn

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        AnimationSubPageHeader {
            Layout.fillWidth: true
        }

    }

}
