// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

/**
 * @brief Animations → OSD — on-screen display and feedback events.
 *
 * Events from `PhosphorAnimation::ProfilePaths` osd.* group.
 */
AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "osd"
        eventLabel: i18n("All OSD Events")
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
        eventPath: "osd.pop"
        eventLabel: i18n("Pop (scale-in)")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "osd.dim"
        eventLabel: i18n("Dim")
    }

}
