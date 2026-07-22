// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// OSD animation events: the `osd.*` resolver subtree (both LayoutOsd and
// NavigationOsd modes). "All OSDs" is the parent-node cascade whose override
// applies to `osd.show` and `osd.hide` via ShaderProfileTree walk-up. Those two
// carry the appearance shader contract, so their rows show the (filtered) shader
// picker. `osd.pop` is the scale-pop leg the SurfaceAnimator plays as the OSD
// appears; it has its own curve, tuned independently of the show fade, and is
// timing-only (not shader-driven), so its row shows just the timing controls.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("OSD animation events")
    headerText: i18n("Animations for on-screen displays. \"All OSDs\" is the default. Each event can override it.")
    eventModel: [
        {
            "eventPath": "osd",
            "eventLabel": i18n("All OSDs"),
            "isParentNode": true
        },
        {
            "eventPath": "osd.show",
            "eventLabel": i18n("Shown"),
            "isParentNode": false
        },
        {
            "eventPath": "osd.pop",
            "eventLabel": i18n("Emphasized"),
            "isParentNode": false
        },
        {
            "eventPath": "osd.hide",
            "eventLabel": i18n("Hidden"),
            "isParentNode": false
        }
    ]
}
