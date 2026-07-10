// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Window MOVEMENT events (a window changing geometry, cross-faded from its old
// rect to its new one: move, resize, maximize, snap in/out, layout switch). The
// window APPEARANCE events (open/close/...) live on the Transitions → Windows
// page under their own window.appearance parent.
//
// "All Windows" is the real window.movement cascade parent: its shader + timing
// apply to the movement leaves by inheritance, an individual override shadows
// it, and clearing a child re-inherits. Independent of the appearance page's
// "All" (a sibling under `window`, not an ancestor). `window.movement.snapResize`
// is omitted (no kwin-effect callsite routes it, so a row would be runtime-dead).
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Window movement animation events")
    headerText: i18n("Animations for windows moving and snapping. \"All Windows\" is the default. Each event can override it.")
    eventModel: [
        {
            "eventPath": "window.movement",
            "eventLabel": i18n("All Windows"),
            "isParentNode": true
        },
        {
            "eventPath": "window.movement.move",
            "eventLabel": i18n("Moved"),
            "isParentNode": false
        },
        {
            "eventPath": "window.movement.resize",
            "eventLabel": i18n("Resized"),
            "isParentNode": false
        },
        {
            "eventPath": "window.movement.maximize",
            "eventLabel": i18n("Maximized"),
            "isParentNode": false
        },
        {
            "eventPath": "window.movement.snapIn",
            "eventLabel": i18n("Snapped Into Zone"),
            "isParentNode": false
        },
        {
            "eventPath": "window.movement.snapOut",
            "eventLabel": i18n("Snapped Out of Zone"),
            "isParentNode": false
        },
        {
            "eventPath": "window.movement.layoutSwitch",
            "eventLabel": i18n("Layout Switched"),
            "isParentNode": false
        }
    ]
}
