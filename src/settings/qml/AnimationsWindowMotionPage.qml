// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Window MOVEMENT events (a window changing geometry, cross-faded from its old
// rect to its new one: move, resize, maximize, snap in/out, layout switch).
// Window APPEARANCE events (open/close/...) live on the Transitions → Windows
// page. Each row's shader picker is filtered to the geometry-morph set.
//
// "All Windows" here is a scoped group applicator over the movement leaves (its
// own "All", independent of the appearance page's). Representative path is
// window.move. `window.snapResize` is omitted (no kwin-effect callsite routes
// it, so a row would be runtime-dead).
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Window movement animation events")
    eventModel: [
        {
            "eventPath": "window.move",
            "eventLabel": i18n("All Windows"),
            "isParentNode": true,
            "groupPaths": ["window.move", "window.resize", "window.maximize", "window.snapIn", "window.snapOut", "window.layoutSwitch"]
        },
        {
            "eventPath": "window.move",
            "eventLabel": i18n("Moved"),
            "isParentNode": false
        },
        {
            "eventPath": "window.resize",
            "eventLabel": i18n("Resized"),
            "isParentNode": false
        },
        {
            "eventPath": "window.maximize",
            "eventLabel": i18n("Maximized"),
            "isParentNode": false
        },
        {
            "eventPath": "window.snapIn",
            "eventLabel": i18n("Snapped Into Zone"),
            "isParentNode": false
        },
        {
            "eventPath": "window.snapOut",
            "eventLabel": i18n("Snapped Out of Zone"),
            "isParentNode": false
        },
        {
            "eventPath": "window.layoutSwitch",
            "eventLabel": i18n("Layout Switch"),
            "isParentNode": false
        }
    ]
}
