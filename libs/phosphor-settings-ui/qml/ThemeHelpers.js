.pragma library

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Small theme/color helpers used across the chrome QML. Centralised so
// the same idiom doesn't multiply across UnsavedChangesFooter, Sidebar*,
// and downstream consumer pages.

/**
 * Return @p baseColor with its alpha channel replaced by @p alpha
 * (0.0..1.0). Replaces the verbose `Qt.rgba(c.r, c.g, c.b, a)` idiom
 * — same arithmetic, single named entry point so theme-tint sites are
 * grep-able and a future re-target (e.g. to Kirigami.ColorUtils once
 * its alpha helper stabilises across KF6 minors) touches one place.
 */
function withAlpha(baseColor, alpha) {
    return Qt.rgba(baseColor.r, baseColor.g, baseColor.b, alpha);
}

// Conventional alpha presets — pre-named so the call sites read as
// "highlightTintActive(color)" instead of "withAlpha(color, 0.12)"
// where the 0.12 carries semantic meaning the literal doesn't.
//
// Values match the tints the legacy Phosphor chrome shipped — a
// future visual-tweak pass can adjust them in one place.
var ACTIVE_TINT_ALPHA = 0.12;   // Active-row highlight background
var HOVER_TINT_ALPHA = 0.06;    // Hover background
var SUBTLE_BACKGROUND_ALPHA = 0.15; // Less-busy "I'm a placeholder" tint
var SUBTLE_OUTLINE_ALPHA = 0.2; // Outline / divider tint

function activeTint(themeColor) {
    return withAlpha(themeColor, ACTIVE_TINT_ALPHA);
}

function hoverTint(themeColor) {
    return withAlpha(themeColor, HOVER_TINT_ALPHA);
}
