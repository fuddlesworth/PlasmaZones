// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Parsing helpers for the scrolling preset lists, whose stored form is a
// canonical comma-joined fraction string. The Columns page's preset-index
// spins and PresetListEditor both need the entry count, and an empty string
// has to read as zero entries rather than one, so the split lives here
// instead of being re-spelled per consumer.
.pragma library

/// The stored string as an array of raw entries. Empty string means no
/// entries, which a bare String.split would report as one.
function values(presets) {
    return (presets && presets.length > 0) ? presets.split(",") : [];
}

/// How many presets the stored string holds.
function count(presets) {
    return values(presets).length;
}

/// One entry as a usable [0, 1] fraction. A hand-edited config can hold
/// anything, and an unguarded parseFloat would paint a "NaN%" label over a
/// band of no width, so anything unusable reads as 0.
function fraction(raw) {
    var v = parseFloat(raw);
    if (isNaN(v))
        return 0;
    return Math.max(0, Math.min(1, v));
}

/// One entry as the whole percent the UI labels it with.
function percent(raw) {
    return Math.round(fraction(raw) * 100);
}
