// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Scrolling-strip view motion. One event, and its subject is the VIEW rather
// than any window: a scroll moves every column on the strip by the same
// amount, so the compositor springs the view once per monitor and every column
// rides that one offset. The tab-indicator overlay mirrors the same profile
// locally, because it is drawn into a layer-shell surface that the
// compositor's paint offset never reaches.
//
// Curve and duration only. The leg translates already-painted windows — no
// capture, no surface of its own, no old-rect/new-rect pair to hand a pack —
// so shaderEffectAppliesToEventPath dims every effect here rather than let a
// user assign one that could only be ignored.
//
// No "All Scrolling Events" parent row: a cascade over a single child is
// noise, the same reason the Desktop page left its parent off while `switch`
// was its only leaf. Add one if a second leg ever lands.
AnimationEventCardList {
    Accessible.name: i18n("Scrolling animation events")
    headerText: i18n("Animation for the scrolling strip. The whole strip moves together, so this is one setting for every column.")
    eventModel: [
        {
            "eventPath": "scrolling.view",
            "eventLabel": i18n("Strip Scrolled"),
            "isParentNode": false
        }
    ]
}
