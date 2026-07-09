// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Appearance transitions: a single surface materialising or dissolving in or
// out. Every row here takes the same shader contract (appearance /
// single-surface), so the picker offers one coherent set. Covers window
// lifecycle (open/close/minimize/focus), OSD show·hide, and the popup overlays.
//
// The window rows have no parent node: `window` spans both contracts (its
// move/resize legs live on the Movement page), so a `window` shader would leak
// across pages. OSD and popup keep their family parents, whose whole subtree is
// appearance and stays on this page.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Appearance animation events")
    eventModel: [
        {
            "eventPath": "window.open",
            "eventLabel": i18n("Window Opened"),
            "isParentNode": false
        },
        {
            "eventPath": "window.close",
            "eventLabel": i18n("Window Closed"),
            "isParentNode": false
        },
        {
            "eventPath": "window.minimize",
            "eventLabel": i18n("Window Minimized"),
            "isParentNode": false
        },
        {
            "eventPath": "window.focus",
            "eventLabel": i18n("Window Focused"),
            "isParentNode": false
        },
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
        },
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
