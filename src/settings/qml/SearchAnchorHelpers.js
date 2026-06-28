// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// Shared parent-chain walks for the search-reveal registry, used by every
// component that registers a searchAnchor (SettingsRow, SettingsCard,
// RuleSectionList, AnimationEventCardList).

// The page's reveal registry: the first ancestor exposing registerSearchAnchor
// (a SettingsFlickable). Returns null if none is found.
function pageFor(item) {
    var p = item ? item.parent : null;
    while (p) {
        if (typeof p.registerSearchAnchor === "function")
            return p;

        p = p.parent;
    }
    return null;
}

// The hosting SettingsCard (carries isSettingsCard === true), so a contained
// row can ask the card to expand on reveal. Returns null if none is found.
function cardFor(item) {
    var p = item ? item.parent : null;
    while (p) {
        if (p.isSettingsCard === true)
            return p;

        p = p.parent;
    }
    return null;
}
