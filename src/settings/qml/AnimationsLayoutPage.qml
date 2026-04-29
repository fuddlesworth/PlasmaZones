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
            eventPath: "zone.layoutSwitchIn"
            eventLabel: i18n("Layout Switch In")
            eventDescription: i18n("Switching to a new zone layout")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "zone.layoutSwitchOut"
            eventLabel: i18n("Layout Switch Out")
            eventDescription: i18n("Leaving the current zone layout")
        }
    }
}
