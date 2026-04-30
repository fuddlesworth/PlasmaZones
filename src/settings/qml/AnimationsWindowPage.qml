// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

/**
 * @brief Animations → Window — per-window lifecycle and geometry events.
 *
 * Events from `PhosphorAnimation::ProfilePaths` window.* group.
 */
AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "window"
        eventLabel: i18n("All Window Events")
        isParentNode: true
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "window.open"
        eventLabel: i18n("Open")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "window.close"
        eventLabel: i18n("Close")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "window.minimize"
        eventLabel: i18n("Minimize")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "window.unminimize"
        eventLabel: i18n("Unminimize")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "window.maximize"
        eventLabel: i18n("Maximize")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "window.unmaximize"
        eventLabel: i18n("Unmaximize")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "window.move"
        eventLabel: i18n("Move")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "window.resize"
        eventLabel: i18n("Resize")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "window.focus"
        eventLabel: i18n("Focus")
    }

}
