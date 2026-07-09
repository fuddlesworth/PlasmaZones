// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Overlay animation events: the transient popups invoked by user action (zone
// selector, layout picker, snap assist), each with a show and hide leg. "All
// Overlays" is the `popup` cascade parent. All rows are the appearance
// contract; the shader picker is filtered accordingly.
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
            "eventLabel": i18n("Zone Selector Shown"),
            "isParentNode": false
        },
        {
            "eventPath": "popup.zoneSelector.hide",
            "eventLabel": i18n("Zone Selector Hidden"),
            "isParentNode": false
        },
        {
            "eventPath": "popup.layoutPicker.show",
            "eventLabel": i18n("Layout Picker Shown"),
            "isParentNode": false
        },
        {
            "eventPath": "popup.layoutPicker.hide",
            "eventLabel": i18n("Layout Picker Hidden"),
            "isParentNode": false
        },
        {
            "eventPath": "popup.snapAssist.show",
            "eventLabel": i18n("Snap Assist Shown"),
            "isParentNode": false
        },
        {
            "eventPath": "popup.snapAssist.hide",
            "eventLabel": i18n("Snap Assist Hidden"),
            "isParentNode": false
        }
    ]
}
