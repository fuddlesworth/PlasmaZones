// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "popup"
        eventLabel: i18n("All Popup Events")
        isParentNode: true
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "layoutPickerIn"
        eventLabel: i18n("Layout Picker In")
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "layoutPickerOut"
        eventLabel: i18n("Layout Picker Out")
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "snapAssistIn"
        eventLabel: i18n("Snap Assist In")
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "snapAssistOut"
        eventLabel: i18n("Snap Assist Out")
        styleDomain: "overlay"
    }

}
