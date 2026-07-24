// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Window MOVEMENT events (a window changing geometry, cross-faded from its old
// rect to its new one: maximize, snap in/out, layout switch). The window
// APPEARANCE events (open/close/...) live on the Transitions → Windows page
// under their own window.appearance parent.
//
// "All Windows" is the real window.movement cascade parent: its shader + timing
// apply to the movement leaves by inheritance, an individual override shadows
// it, and clearing a child re-inherits. Independent of the appearance page's
// "All" (a sibling under `window`, not an ancestor).
// `window.movement.move` lives on its own Window Dragging page: the held
// interactive drag is its own opt-in shader class (`move`) and takes no
// inherited shader, so a row here under "All Windows" would misrepresent the
// cascade — see AnimationsWindowDraggingPage.qml. There are no resize rows:
// the interactive-resize and snapResize events were dropped from the taxonomy
// (held resizes have nothing to animate; discrete resizes are the snap /
// layout-switch / maximize events).
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
