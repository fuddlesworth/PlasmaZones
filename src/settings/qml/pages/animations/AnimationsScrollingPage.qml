// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Scrolling-strip view motion. One event, and its subject is the VIEW rather
// than any window: a scroll moves every column on the strip by the same
// amount, so the compositor springs the view once per monitor and every column
// rides that one offset. The tab indicators ride it too: the compositor adds
// the same offset to their surface in the same paint pass, so this one profile
// governs the whole thing and nothing mirrors it on the daemon side.
//
// Curve, duration AND a shader leg. The shader picker offers only the strip
// class (appliesTo ["strip"], e.g. Strip Motion Blur): the view spring
// retargets continuously under wheel scrolling, so there is no from/to pair
// for a crossfade pack, so availableShaderEffectsForPath FILTERS the list
// down to the packs that consume the strip contract (uStrip / iStripMotion
// via strip_transition.glsl). Nothing is shown dimmed — the incompatible
// packs are simply absent. The pass decorates the strip and what lies under
// it, per output, while the spring is in flight (StripTransitionManager);
// anything stacked above the strip is composited sharp on top afterwards.
// With no pack assigned the strip scrolls exactly as before.
//
// The tab-switch leg is the second event here, and it is a different animal
// from the first despite sharing the root. Its subject IS a window: activating
// a tab parks the one that was showing and puts the new one in the rect it
// vacated, so the compositor cross-fades a capture of the outgoing tab into
// the arriving one's live content. Two textures on a window quad, which is the
// `tab` class (appliesTo ["tab"], e.g. Tab Fade), and its picker filters to
// those packs the same way the view row filters to strip packs.
//
// Still NO "All Scrolling Events" parent row, and now for a stronger reason
// than the old one. The Desktop page grew its parent when it gained a second
// leg because both of its legs share the desktop class, so a pack on the root
// validates and cascades to both. These two do not: the root carries the strip
// class and the tab leaf carries its own, so a pack assigned at `scrolling`
// could never be valid for both children. A parent row would offer a choice
// that cannot mean anything for half the page.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Scrolling animation events")
    headerText: i18n("Animations for scrolling. Strip Scrolled covers the whole strip moving together, and Tab Switched covers a tab replacing another in its column. Each event offers only the shaders that can drive it.")
    eventModel: [
        {
            "eventPath": "scrolling.view",
            "eventLabel": i18n("Strip Scrolled"),
            "isParentNode": false
        },
        {
            "eventPath": "scrolling.tabSwitch",
            "eventLabel": i18n("Tab Switched"),
            "isParentNode": false
        }
    ]
}
