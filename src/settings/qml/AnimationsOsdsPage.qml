// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// OSD animation events: the `osd.*` resolver subtree (both LayoutOsd and
// NavigationOsd modes). "All OSDs" is the parent-node cascade whose override
// applies to `osd.show` and `osd.hide` via ShaderProfileTree walk-up. All rows
// are the appearance contract; the shader picker is filtered accordingly.
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
            "eventLabel": i18n("Shown"),
            "isParentNode": false
        },
        {
            "eventPath": "osd.hide",
            "eventLabel": i18n("Hidden"),
            "isParentNode": false
        }
    ]
}
