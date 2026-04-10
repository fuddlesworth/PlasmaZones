// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "preview"
        eventLabel: i18n("All Preview Events")
        isParentNode: true
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "previewIn"
        eventLabel: i18n("Preview In")
        styleDomain: "overlay"
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventName: "previewOut"
        eventLabel: i18n("Preview Out")
        styleDomain: "overlay"
    }

}
