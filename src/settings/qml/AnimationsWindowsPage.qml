// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
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

        // Snap-into-zone window animations driven by the kwin-effect.
        // The window quad animates when it snaps into / out of a zone
        // or when a layout switch repositions it.
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "window.snapIn"
            eventLabel: i18n("Snap Into Zone")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "window.snapOut"
            eventLabel: i18n("Snap Out of Zone")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "window.snapResize"
            eventLabel: i18n("Snap Resize")
        }

        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "window.layoutSwitch"
            eventLabel: i18n("Layout Switch")
        }

    }

}
