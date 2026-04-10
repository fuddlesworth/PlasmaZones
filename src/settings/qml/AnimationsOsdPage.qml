// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "osd"
        eventLabel: i18n("All OSD Events")
        isParentNode: true
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "layoutOsdIn"
        eventLabel: i18n("Layout OSD In")
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "layoutOsdOut"
        eventLabel: i18n("Layout OSD Out")
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "navigationOsdIn"
        eventLabel: i18n("Navigation OSD In")
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "navigationOsdOut"
        eventLabel: i18n("Navigation OSD Out")
        styleDomain: "overlay"
    }

}
