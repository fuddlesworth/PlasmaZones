// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    contentHeight: col.implicitHeight
    clip: true
    Accessible.name: i18n("Window animation events")
    ColumnLayout {
        id: col
        width: parent.width
        spacing: Kirigami.Units.smallSpacing
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "window"
            eventLabel: i18n("All Window Events")
            isParentNode: true
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "window.open"
            eventLabel: i18n("Open")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "window.close"
            eventLabel: i18n("Close")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "window.minimize"
            eventLabel: i18n("Minimize")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "window.maximize"
            eventLabel: i18n("Maximize")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "window.move"
            eventLabel: i18n("Move")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "window.resize"
            eventLabel: i18n("Resize")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "window.focus"
            eventLabel: i18n("Focus")
        }
    }
}
