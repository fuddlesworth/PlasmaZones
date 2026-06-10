// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Widget animation events. Viewport-virtualized via AnimationEventCardList
// (only visible AnimationEventCards build) — see that component.
AnimationEventCardList {
    Accessible.name: i18n("Widget animation events")
    eventModel: [
        {
            "eventPath": "widget",
            "eventLabel": i18n("All Widget Events"),
            "isParentNode": true
        },
        {
            "eventPath": "widget.hover",
            "eventLabel": i18n("Hover"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.press",
            "eventLabel": i18n("Press"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.toggleOn",
            "eventLabel": i18n("Toggle On"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.toggleOff",
            "eventLabel": i18n("Toggle Off"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.badgeShow",
            "eventLabel": i18n("Show (badge)"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.badgeHide",
            "eventLabel": i18n("Hide (badge)"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.badgePulse",
            "eventLabel": i18n("Pulse (badge)"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.tint",
            "eventLabel": i18n("Tint"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.dim",
            "eventLabel": i18n("Dim"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.fadeIn",
            "eventLabel": i18n("Fade In"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.fadeOut",
            "eventLabel": i18n("Fade Out"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.reorder",
            "eventLabel": i18n("Reorder"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.accordionExpand",
            "eventLabel": i18n("Expand (accordion)"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.accordionCollapse",
            "eventLabel": i18n("Collapse (accordion)"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.progress",
            "eventLabel": i18n("Progress"),
            "isParentNode": false
        },
        // Zone-rect widget. Embedded across the runtime overlay, settings
        // dialogs, layout thumbnails, the new-layout dialog, shared
        // previews. The animation lives with the widget; the surface
        // hosting it is incidental.
        {
            "eventPath": "widget.zoneHighlight",
            "eventLabel": i18n("Zone Highlight"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.zoneHighlight.pop",
            "eventLabel": i18n("Zone Highlight: Pop"),
            "isParentNode": false
        },
        {
            "eventPath": "widget.zoneHighlight.border",
            "eventLabel": i18n("Zone Highlight: Border"),
            "isParentNode": false
        },
        // One-shot flash on the main zone-overlay surface when the active
        // layout switches mid-drag.
        {
            "eventPath": "widget.zoneOverlayFlash",
            "eventLabel": i18n("Zone Overlay: Layout-Switch Flash"),
            "isParentNode": false
        },
        {
            "eventPath": "cursor.hover",
            "eventLabel": i18n("Cursor Hover"),
            "isParentNode": false
        },
        {
            "eventPath": "cursor.click",
            "eventLabel": i18n("Cursor Click"),
            "isParentNode": false
        }
    ]
}
