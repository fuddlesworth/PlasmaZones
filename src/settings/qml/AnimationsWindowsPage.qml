// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Window APPEARANCE events (a window materialising or dissolving: open, close,
// minimize, focus). The window MOVEMENT events (move/resize/snap/...) live on
// the Motion → Windows page under their own window.movement parent.
//
// "All Windows" is the real window.appearance cascade parent (osd/popup
// pattern): its shader + timing apply to the four leaves by inheritance, an
// individual event override shadows it, and clearing a child re-inherits. It
// does NOT reach the movement leaves (those hang off window.movement).
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Window appearance animation events")
    headerText: i18n("Animations for windows opening and closing. \"All Windows\" is the default. Each event can override it.")
    eventModel: [
        {
            "eventPath": "window.appearance",
            "eventLabel": i18n("All Windows"),
            "isParentNode": true
        },
        {
            "eventPath": "window.appearance.open",
            "eventLabel": i18n("Opened"),
            "isParentNode": false
        },
        {
            "eventPath": "window.appearance.close",
            "eventLabel": i18n("Closed"),
            "isParentNode": false
        },
        {
            "eventPath": "window.appearance.minimize",
            "eventLabel": i18n("Minimized"),
            "isParentNode": false
        },
        {
            "eventPath": "window.appearance.focus",
            "eventLabel": i18n("Focused"),
            "isParentNode": false
        }
    ]
}
