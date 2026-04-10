// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "zoneHighlight"
        eventLabel: i18n("All Zone Highlight Events")
        isParentNode: true
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "zoneHighlightIn"
        eventLabel: i18n("Zone Highlight In")
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "zoneHighlightOut"
        eventLabel: i18n("Zone Highlight Out")
        styleDomain: "overlay"
    }

}
