// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "layoutSwitch"
        eventLabel: i18n("All Layout Switch Events")
        isParentNode: true
        styleDomain: "window"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "layoutSwitchIn"
        eventLabel: i18n("Layout Switch In")
        styleDomain: "window"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "layoutSwitchOut"
        eventLabel: i18n("Layout Switch Out")
        styleDomain: "window"
    }

}
