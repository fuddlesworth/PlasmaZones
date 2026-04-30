// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

/**
 * @brief Animations → Zone sub-page.
 *
 * Edits PlasmaZones-native zone events from `PhosphorAnimation::ProfilePaths`.
 * `zone.layoutSwitchOut` is reserved (no consumer in the current build) and
 * intentionally excluded — see ProfilePaths.h:isReservedPath.
 */
AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "zone"
        eventLabel: i18n("All Zone Events")
        isParentNode: true
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "zone.snapIn"
        eventLabel: i18n("Snap In")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "zone.snapOut"
        eventLabel: i18n("Snap Out")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "zone.snapResize"
        eventLabel: i18n("Snap Resize")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "zone.highlight"
        eventLabel: i18n("Highlight")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "zone.layoutSwitchIn"
        eventLabel: i18n("Layout Switch")
    }

}
