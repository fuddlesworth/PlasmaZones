// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "zone.highlight"
            eventLabel: i18n("Zone Highlight")
            eventDescription: i18n("Zone highlighting during drag operations")
        }
    }
}
