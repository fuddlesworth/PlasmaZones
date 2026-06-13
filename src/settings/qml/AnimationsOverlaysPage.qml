// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Overlays animation page: the `popup.*` resolver subtree (zone selector,
// layout picker, snap assist; the resolver path keeps the historical
// name). The "All Overlays" parent is a parent-node card whose override
// cascades to `popup.zoneSelector.*`, `popup.layoutPicker.*` and
// `popup.snapAssist.*` via ShaderProfileTree::resolve's walk-up (`popup`
// is the closest common ancestor of all three surfaces). In-app side
// panels (settings nav rail, editor property panel) live under `panel.*`
// and have their own dedicated Side Panels page.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Overlay animation events")
    eventModel: [
        {
            "eventPath": "popup",
            "eventLabel": i18n("All Overlays"),
            "isParentNode": true
        },
        {
            "eventPath": "popup.zoneSelector.show",
            "eventLabel": i18n("Zone Selector: Show"),
            "isParentNode": false
        },
        {
            "eventPath": "popup.zoneSelector.hide",
            "eventLabel": i18n("Zone Selector: Hide"),
            "isParentNode": false
        },
        {
            "eventPath": "popup.layoutPicker.show",
            "eventLabel": i18n("Layout Picker: Show"),
            "isParentNode": false
        },
        {
            "eventPath": "popup.layoutPicker.hide",
            "eventLabel": i18n("Layout Picker: Hide"),
            "isParentNode": false
        },
        {
            "eventPath": "popup.snapAssist.show",
            "eventLabel": i18n("Snap Assist: Show"),
            "isParentNode": false
        },
        {
            "eventPath": "popup.snapAssist.hide",
            "eventLabel": i18n("Snap Assist: Hide"),
            "isParentNode": false
        }
    ]
}
