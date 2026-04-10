// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "snap"
        eventLabel: i18n("All Snap Events")
        isParentNode: true
        styleDomain: "window"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "snapIn"
        eventLabel: i18n("Snap In")
        styleDomain: "window"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "snapOut"
        eventLabel: i18n("Snap Out")
        styleDomain: "window"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "snapResize"
        eventLabel: i18n("Snap Resize")
        styleDomain: "window"
    }

}
