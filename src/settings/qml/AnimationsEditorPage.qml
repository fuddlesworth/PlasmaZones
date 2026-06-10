// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Layout-editor zone-manipulation animations. Fires only inside the
// PlasmaZones layout editor (fill-preview, snap-resize-preview). Runtime
// window snapping is KWin's compositor-level domain and is NOT controlled
// here.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Layout editor animation events")
    eventModel: [
        {
            "eventPath": "editor",
            "eventLabel": i18n("All Editor Events"),
            "isParentNode": true
        },
        {
            "eventPath": "editor.snapIn",
            "eventLabel": i18n("Snap Into Zone (Fill Preview)"),
            "isParentNode": false
        },
        {
            "eventPath": "editor.snapOut",
            "eventLabel": i18n("Snap Out of Zone"),
            "isParentNode": false
        },
        {
            "eventPath": "editor.snapResize",
            "eventLabel": i18n("Snap Resize (Drag Preview)"),
            "isParentNode": false
        }
    ]
}
