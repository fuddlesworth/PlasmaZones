// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Window DRAGGING page: the `window.movement.move` leaf on its own. An
// interactive drag installs a HELD transition the crossfade packs cannot
// drive, so the leaf is its own opt-in shader class (`move`, see
// EventClassMove): its picker offers only move-class packs (wobble), it
// takes NO inherited shader (ShaderProfileTree::resolve leaf isolation),
// and it carries no built-in default. That is why it does NOT sit on the
// Window Motion page under the "All Windows" cascade parent — the parent's
// shader could never apply to it, and a cascade row whose "All" never
// reaches it would misrepresent the inheritance. Timing (duration / curve)
// still inherits from window.movement → window → global as usual.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Window dragging animation event")
    headerText: i18n("Animation while you drag a window. Drag shaders are physics driven and follow the pointer until you let go, so only those shaders are offered here.")
    eventModel: [
        {
            "eventPath": "window.movement.move",
            "eventLabel": i18n("Dragged"),
            "isParentNode": false
        }
    ]
}
