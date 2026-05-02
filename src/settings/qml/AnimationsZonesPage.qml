// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    contentHeight: col.implicitHeight
    clip: true
    ColumnLayout {
        id: col
        width: parent.width
        spacing: Kirigami.Units.smallSpacing
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "zone"
            eventLabel: i18n("All Zone Events")
            isParentNode: true
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "zone.snapIn"
            eventLabel: i18n("Snap In")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "zone.snapResize"
            eventLabel: i18n("Snap Resize")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "zone.highlight"
            eventLabel: i18n("Highlight")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "zone.layoutSwitchIn"
            eventLabel: i18n("Layout Switch")
        }
    }
}
