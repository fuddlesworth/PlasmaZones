// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Virtual-desktop animation events. Unlike the per-window / per-surface
// pages, a desktop switch is a full-screen two-texture transition (the
// outgoing desktop blended into the incoming one), driven by the
// kwin-effect's DesktopTransitionManager. Only desktop-class shaders
// (metadata appliesTo ["desktop"], e.g. Desktop Fade) are selectable here;
// single-surface effects are dimmed by eventClassForPath's opt-in desktop
// class.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Virtual desktop animation events")
    eventModel: [
        {
            "eventPath": "desktop",
            "eventLabel": i18n("All Desktop Events"),
            "isParentNode": true
        },
        {
            "eventPath": "desktop.switch",
            "eventLabel": i18n("Switch Desktop"),
            "isParentNode": false
        }
    ]
}
