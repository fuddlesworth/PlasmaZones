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
            eventPath: "osd.show"
            eventLabel: i18n("OSD Show")
            eventDescription: i18n("On-screen display appearing")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "osd.hide"
            eventLabel: i18n("OSD Hide")
            eventDescription: i18n("On-screen display disappearing")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "osd.dim"
            eventLabel: i18n("OSD Dim")
            eventDescription: i18n("On-screen display dimming")
        }
    }
}
