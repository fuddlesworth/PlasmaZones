// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Window APPEARANCE events (a window materialising or dissolving: open, close,
// minimize, focus). The window MOVEMENT events (move/resize/snap/...) live on
// the Motion → Windows page. Each row's shader picker is filtered to the
// appearance-compatible set.
//
// "All Windows" here is a scoped group applicator over the four appearance
// leaves (not the `window` path cascade, which would also hit the movement
// events on the other page). Its representative path is window.open.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Window appearance animation events")
    eventModel: [
        {
            "eventPath": "window.open",
            "eventLabel": i18n("All Windows"),
            "isParentNode": true,
            "groupPaths": ["window.open", "window.close", "window.minimize", "window.focus"]
        },
        {
            "eventPath": "window.open",
            "eventLabel": i18n("Opened"),
            "isParentNode": false
        },
        {
            "eventPath": "window.close",
            "eventLabel": i18n("Closed"),
            "isParentNode": false
        },
        {
            "eventPath": "window.minimize",
            "eventLabel": i18n("Minimized"),
            "isParentNode": false
        },
        {
            "eventPath": "window.focus",
            "eventLabel": i18n("Focused"),
            "isParentNode": false
        }
    ]
}
