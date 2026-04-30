// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts

/**
 * @brief Animations → Cursor — pointer / input feedback events.
 *
 * Events from `PhosphorAnimation::ProfilePaths` cursor.* group.
 * `cursor.drag` is reserved (the kwin-effect's previous wire-up was
 * shadowed by `window.move`); intentionally excluded.
 */
AnimationSubPage {
    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "cursor"
        eventLabel: i18n("All Cursor Events")
        isParentNode: true
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "cursor.hover"
        eventLabel: i18n("Hover")
    }

    AnimationEventCard {
        Layout.fillWidth: true
        eventPath: "cursor.click"
        eventLabel: i18n("Click")
    }

}
