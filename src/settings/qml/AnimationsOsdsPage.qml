// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// OSDs animation page: the `osd.*` resolver subtree (both LayoutOsd and
// NavigationOsd modes). The "All OSDs" parent is a parent-node card whose
// override cascades to `osd.show` and `osd.hide` via
// ShaderProfileTree::resolve's walk-up. Transient overlays (zone selector,
// layout picker, snap assist) live under `popup.*` and have their own
// dedicated Overlays page.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("OSD animation events")
    eventModel: [
        {
            "eventPath": "osd",
            "eventLabel": i18n("All OSDs"),
            "isParentNode": true
        },
        {
            "eventPath": "osd.show",
            "eventLabel": i18n("Show"),
            "isParentNode": false
        },
        {
            "eventPath": "osd.hide",
            "eventLabel": i18n("Hide"),
            "isParentNode": false
        }
    ]
}
