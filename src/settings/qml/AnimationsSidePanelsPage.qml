// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Side panels animation page: the `panel.*` resolver subtree (the
// resolver path keeps the historical name). Covers in-app side surfaces
// that slide in from an edge (settings nav rail, editor property panel).
// Slide is size/translate motion; fade is opacity. Renamed from "Panels"
// to disambiguate from system panels (Plasma taskbar, etc.) which we do
// not animate. Transient overlays (zone selector, layout picker, snap
// assist) live under `popup.*` and have their own dedicated Overlays page.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Side panel animation events")
    eventModel: [
        {
            "eventPath": "panel",
            "eventLabel": i18n("All Side Panels"),
            "isParentNode": true
        },
        {
            "eventPath": "panel.slideIn",
            "eventLabel": i18n("Slide In"),
            "isParentNode": false
        },
        {
            "eventPath": "panel.slideOut",
            "eventLabel": i18n("Slide Out"),
            "isParentNode": false
        },
        {
            "eventPath": "panel.fadeIn",
            "eventLabel": i18n("Fade In"),
            "isParentNode": false
        },
        {
            "eventPath": "panel.fadeOut",
            "eventLabel": i18n("Fade Out"),
            "isParentNode": false
        }
    ]
}
