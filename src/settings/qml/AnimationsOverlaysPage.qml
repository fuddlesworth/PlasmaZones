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
            eventPath: "osd"
            eventLabel: i18n("All Overlay Events")
            isParentNode: true
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "osd.show"
            eventLabel: i18n("Show")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "osd.hide"
            eventLabel: i18n("Hide")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.popup.zoneSelector.show"
            eventLabel: i18n("Zone Selector — Show")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.popup.zoneSelector.hide"
            eventLabel: i18n("Zone Selector — Hide")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.popup.layoutPicker.show"
            eventLabel: i18n("Layout Picker — Show")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.popup.layoutPicker.hide"
            eventLabel: i18n("Layout Picker — Hide")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.popup.snapAssist.show"
            eventLabel: i18n("Snap Assist — Show")
        }
        AnimationEventCard {
            Layout.fillWidth: true
            eventPath: "panel.slideIn"
            eventLabel: i18n("Slide In")
        }
    }
}
