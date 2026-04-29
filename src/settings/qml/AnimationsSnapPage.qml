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
            eventPath: "zone.snapIn"
            eventLabel: i18n("Snap In")
            eventDescription: i18n("Window snapping into a zone")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "zone.snapOut"
            eventLabel: i18n("Snap Out")
            eventDescription: i18n("Window leaving a zone")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "zone.snapResize"
            eventLabel: i18n("Snap Resize")
            eventDescription: i18n("Zone resizing while window is snapped")
        }
    }
}
