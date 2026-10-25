// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library

/**
 * True when a stored rule-condition value names the same entry as a value
 * from a picker's list. Strings match ignoring case, as the rule engine's
 * Equals compares a condition, so a hand-edited id in another case still
 * reads as the entry it matches. Anything else matches only itself.
 *
 * Conditions only: an action's value is looked up exactly by whatever applies
 * it, so the action editors keep an exact compare.
 *
 * @param {var} a - one value
 * @param {var} b - the other
 * @returns {bool}
 */
function same(a, b) {
    if (typeof a === "string" && typeof b === "string")
        return a.toLowerCase() === b.toLowerCase();
    return a === b;
}
