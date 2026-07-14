// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Virtual-desktop animation events. Unlike the per-window / per-surface
// pages, these are full-screen two-texture transitions driven by the
// kwin-effect's DesktopTransitionManager: the switch blends the outgoing
// desktop into the incoming one, and the peek blends the windows scene
// against the bare desktop (and back). Only desktop-class shaders (metadata
// appliesTo ["desktop"], e.g. Desktop Fade) are selectable here;
// single-surface effects are filtered out of the shader picker by
// eventClassForPath's opt-in desktop class.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Virtual desktop animation events")
    eventModel: [
        {
            "eventPath": "desktop.switch",
            "eventLabel": i18n("Desktop Switched"),
            "isParentNode": false
        },
        {
            "eventPath": "desktop.peek",
            "eventLabel": i18n("Peeked at Desktop"),
            "isParentNode": false
        }
    ]
}
