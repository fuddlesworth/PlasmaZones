// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The persisted Rule::name label pieces for the per-mode disable rules. Not
// config keys, which is why they are free functions rather than ConfigKeys
// members — they were split out of configkeys.h when that file hit its size
// ceiling. configkeys.h includes this header, so every call site that already
// reached them through ConfigKeys keeps compiling unchanged.

#include <PhosphorZones/AssignmentEntry.h>

#include <QString>

namespace PlasmaZones {

// ─── Disable-rule label helpers ─────────────────────────────────────────────
// Shared between the live Settings disable-list writer
// (Settings::writeDisableEntries) and the v3→v4 migration's disable-rule
// builders. Both call sites must produce the same `Rule::name` string for
// a given (mode, screen, desktop, activity) tuple so that resaving an existing
// disable list (e.g. after a UI edit) doesn't fork into two slightly different
// labels for what is otherwise the same rule.
//
// These are NOT translated. `Rule::name` is the persisted identity
// surface in rules.json; running the app under different locales must
// not change its on-disk text. The rule editor surfaces the name verbatim,
// matching the historic behaviour.
inline QString autotileDisableRulePrefix()
{
    return QStringLiteral("Autotile off · ");
}

inline QString snappingDisableRulePrefix()
{
    return QStringLiteral("Snapping off · ");
}

inline QString scrollingDisableRulePrefix()
{
    return QStringLiteral("Scrolling off · ");
}

/// Persistent label-prefix for the Rule::name field of a per-mode
/// disable rule. Exhaustive switch — a future `Mode` enum value added in
/// `AssignmentEntry.h` without an entry here fires a `Q_UNREACHABLE`
/// diagnostic rather than silently producing an empty prefix that lands
/// in the persisted `Rule::name` as bare ` · DP-1` (parseable but
/// anonymous, and identical across modes — losing the screen→mode
/// affinity that makes the rule editor scannable).
inline QString disableRulePrefixFor(PhosphorZones::AssignmentEntry::Mode mode)
{
    switch (mode) {
    case PhosphorZones::AssignmentEntry::Snapping:
        return snappingDisableRulePrefix();
    case PhosphorZones::AssignmentEntry::Autotile:
        return autotileDisableRulePrefix();
    case PhosphorZones::AssignmentEntry::Scrolling:
        return scrollingDisableRulePrefix();
    }
    Q_UNREACHABLE();
    return QString();
}

inline QString disableRuleDesktopSuffix(int desktop)
{
    return QStringLiteral(" · Desktop ") + QString::number(desktop);
}

inline QString disableRuleActivitySuffix()
{
    return QStringLiteral(" · Activity");
}

} // namespace PlasmaZones
