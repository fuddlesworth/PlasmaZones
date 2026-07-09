// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Movement transitions: a window changing geometry, cross-faded from its old
// rect to its new one. Every row takes the geometry shader contract (an old and
// new rect, e.g. Window Morph), so the picker offers one coherent set.
//
// No parent node: `window` also owns the appearance legs (open/close/etc, on the
// Appearance page), so a `window` shader would cross contracts. `window.snapResize`
// is omitted — no kwin-effect callsite routes a resize-only event, so a row here
// would be runtime-dead.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Movement animation events")
    eventModel: [
        {
            "eventPath": "window.move",
            "eventLabel": i18n("Window Moved"),
            "isParentNode": false
        },
        {
            "eventPath": "window.resize",
            "eventLabel": i18n("Window Resized"),
            "isParentNode": false
        },
        {
            "eventPath": "window.maximize",
            "eventLabel": i18n("Window Maximized"),
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
