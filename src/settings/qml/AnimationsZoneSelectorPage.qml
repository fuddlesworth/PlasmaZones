// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "zoneSelector"
        eventLabel: i18n("All Zone Selector Events")
        isParentNode: true
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "zoneSelectorIn"
        eventLabel: i18n("Zone Selector In")
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "zoneSelectorOut"
        eventLabel: i18n("Zone Selector Out")
        styleDomain: "overlay"
    }

}
