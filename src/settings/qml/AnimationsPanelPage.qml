// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

/**
 * @brief Animations → Panel — docks, bars, popups (zone selector / layout
 *        picker / snap assist).
 *
 * Events from `PhosphorAnimation::ProfilePaths` panel.* group, including
 * the per-leg leaves under panel.popup.* so users can diverge zone selector
 * vs layout picker vs snap assist independently while still inheriting
 * from `panel.popup` as a baseline.
 */
AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "panel"
        eventLabel: i18n("All Panel Events")
        isParentNode: true
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "panel.slideIn"
        eventLabel: i18n("Slide In")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "panel.slideOut"
        eventLabel: i18n("Slide Out")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "panel.popup"
        eventLabel: i18n("All Popups")
        isParentNode: true
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
        eventPath: "panel.popup.layoutPicker.popIn"
        eventLabel: i18n("Layout Picker — Pop In (scale)")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "panel.popup.snapAssist.show"
        eventLabel: i18n("Snap Assist — Show")
    }

}
