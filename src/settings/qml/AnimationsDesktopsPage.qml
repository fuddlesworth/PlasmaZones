// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Virtual-desktop animation events. Unlike the per-window / per-surface
// pages, these are full-screen two-texture transitions driven by the
// kwin-effect's DesktopTransitionManager: the switch blends the outgoing
// desktop into the incoming one, and the peek blends the windows scene
// against the bare desktop (and back). Only desktop-class shaders (metadata
// appliesTo ["desktop"], e.g. Desktop Fade) are selectable here.
// Single-surface effects are filtered out of the shader picker by
// shaderEffectAppliesToEventPath, which makes the desktop class opt-in.
//
// "All Desktop Events" is a REAL cascade parent, not a bulk write: `desktop`
// carries the desktop class itself (profilepaths.cpp), so a pack assigned
// there validates on the root and reaches both legs through the ordinary
// ShaderProfileTree walk-up. Unlike the drag leaf, neither leaf resolves in
// isolation. The row stayed off while `switch` was the only leaf, since a
// cascade over one child is noise; `peek` is the second leg that makes it
// mean something.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Virtual desktop animation events")
    headerText: i18n("Animations for virtual desktops. \"All Desktop Events\" is the default. Each event can override it.")
    eventModel: [
        {
            "eventPath": "desktop",
            "eventLabel": i18n("All Desktop Events"),
            "isParentNode": true
        },
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
