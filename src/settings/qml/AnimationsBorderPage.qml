// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "autotileBorder"
        eventLabel: i18n("All Border Events")
        isParentNode: true
        styleDomain: "window"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "borderIn"
        eventLabel: i18n("Border In")
        styleDomain: "window"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "borderOut"
        eventLabel: i18n("Border Out")
        styleDomain: "window"
    }

}
