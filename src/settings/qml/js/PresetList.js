// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// Parsing helpers for the scrolling preset lists, whose stored form is a
// canonical comma-joined fraction string. The Columns page's preset-index
// spins need the entry count, and an empty string has to read as zero
// entries rather than one, so the split lives here instead of being
// re-spelled per consumer.

/// The stored string as an array of raw entries. Empty string means no
/// entries, which a bare String.split would report as one.
function values(presets) {
    return (presets && presets.length > 0) ? presets.split(",") : [];
}

/// How many presets the stored string holds.
function count(presets) {
    return values(presets).length;
}
