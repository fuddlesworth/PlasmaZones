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
// No "All Scrolling Events" parent row: a cascade over a single child is
// noise, the same reason the Desktop page left its parent off while `switch`
// was its only leaf. Add one if a second leg ever lands.
//
// Card list is viewport-virtualized by AnimationEventCardList.
AnimationEventCardList {
    Accessible.name: i18n("Scrolling animation event")
    headerText: i18n("Animation for the scrolling strip. The whole strip moves together, so this is one setting for every column. Strip shaders decorate that motion as it happens, so only those shaders are offered here.")
    eventModel: [
        {
            "eventPath": "scrolling.view",
            "eventLabel": i18n("Strip Scrolled"),
            "isParentNode": false
        }
    ]
}
