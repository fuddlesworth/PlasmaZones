// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Window animation events. Windows are the one surface that spans two shader
// contracts, so the page is split into "Appearance" (single-surface in/out:
// open/close/minimize/focus) and "Movement" (geometry morph: move/resize/snap/
// maximize/layout-switch) sub-sections. Each row's shader picker is filtered to
// the compatible set for its contract, so the two never mix in a menu.
//
// "All Windows" is the `window` cascade parent: its shader (and reset) applies
// to every window event via ShaderProfileTree walk-up. `window.snapResize` is
// omitted (no kwin-effect callsite routes it, so a row would be runtime-dead).
//
// Card list is viewport-virtualized by AnimationEventCardList; section rows are
// lightweight non-interactive headers.
AnimationEventCardList {
    Accessible.name: i18n("Window animation events")
    eventModel: [
        {
            "eventPath": "window",
            "eventLabel": i18n("All Windows"),
            "isParentNode": true
        },
        {
            "isSection": true,
            "eventLabel": i18n("Appearance")
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
        },
        {
            "isSection": true,
            "eventLabel": i18n("Movement")
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
