// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

/**
 * @brief Animations → Widget — generic UI widget animations.
 *
 * Events from `PhosphorAnimation::ProfilePaths` widget.* group. Each
 * archetype (hover / press / toggle / badge / tint / dim / fade /
 * reorder / accordion / progress) ships with a library default tuned
 * for that motion role; users override individually here.
 */
AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "widget"
        eventLabel: i18n("All Widget Events")
        isParentNode: true
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "widget.hover"
        eventLabel: i18n("Hover")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "widget.press"
        eventLabel: i18n("Press")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "widget.toggle"
        eventLabel: i18n("Toggle (knob slide)")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "widget.badge"
        eventLabel: i18n("Badge (overshoot)")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "widget.tint"
        eventLabel: i18n("Tint (background)")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "widget.dim"
        eventLabel: i18n("Dimension change")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "widget.fade"
        eventLabel: i18n("Fade (opacity)")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "widget.reorder"
        eventLabel: i18n("Reorder")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "widget.accordion"
        eventLabel: i18n("Accordion (expand)")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "widget.progress"
        eventLabel: i18n("Progress")
    }

}
