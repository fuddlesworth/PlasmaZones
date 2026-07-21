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

// Every SettingsCard ancestor, nearest first. A reveal must expand ALL of them:
// expanding only the nearest leaves the row invisible when an outer card is
// also collapsed, and the reveal then falls back to the top of the page for a
// reason nothing on screen explains.
function cardChainFor(item) {
    var chain = [];
    var p = item ? item.parent : null;
    while (p) {
        if (p.isSettingsCard === true)
            chain.push(p);

        p = p.parent;
    }
    return chain;
}
