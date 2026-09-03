// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorcompositor_export.h>

#include <QDBusArgument>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <Qt>

namespace PhosphorCompositor {

/**
 * @brief Pre-parsed activation trigger (avoids QVariant unboxing in hot path)
 *
 * Each trigger has a modifier (enum value) and optional mouseButton bitmask.
 * Parsed once from the QVariantList received via D-Bus.
 */
struct ParsedTrigger
{
    int modifier = 0;
    int mouseButton = 0;

    /// Value equality, so a consumer caching a parsed list can tell a real
    /// settings change from a re-publish of the same one.
    bool operator==(const ParsedTrigger& other) const = default;
};

/**
 * @brief Compositor-agnostic trigger parsing and modifier checking
 *
 * Used by all compositor plugins to parse activation triggers from D-Bus
 * and check if the current modifier/button state matches any trigger.
 */
namespace TriggerParser {

/**
 * @brief Map DragModifier enum value to Qt modifier flags
 *
 * The single table. WindowDragAdaptor::checkModifier() in the daemon used to
 * carry a hand-synced copy and now delegates here, so a new enumerator is
 * added in one place.
 * The enum values are defined in src/core/types/enums.h (DragModifier), whose
 * MaxDragModifier is the authority on the last valid one.
 */
inline bool checkModifier(int modifierSetting, Qt::KeyboardModifiers mods)
{
    const bool shiftHeld = mods.testFlag(Qt::ShiftModifier);
    const bool ctrlHeld = mods.testFlag(Qt::ControlModifier);
    const bool altHeld = mods.testFlag(Qt::AltModifier);
    const bool metaHeld = mods.testFlag(Qt::MetaModifier);

    switch (modifierSetting) {
    case 0:
        return false; // Disabled
    case 1:
        return shiftHeld; // Shift
    case 2:
        return ctrlHeld; // Ctrl
    case 3:
        return altHeld; // Alt
    case 4:
        return metaHeld; // Meta
    case 5:
        return ctrlHeld && altHeld; // CtrlAlt
    case 6:
        return ctrlHeld && shiftHeld; // CtrlShift
    case 7:
        return altHeld && shiftHeld; // AltShift
    case 8:
        return true; // AlwaysActive
    case 9:
        return altHeld && metaHeld; // AltMeta
    case 10:
        return ctrlHeld && altHeld && metaHeld; // CtrlAltMeta
    case 11:
        return metaHeld && shiftHeld; // MetaShift
    case 12:
        return ctrlHeld && metaHeld; // CtrlMeta
    default:
        return false;
    }
}

/**
 * @brief The Qt modifier mask a DragModifier enumerator stands for.
 *
 * The inverse of the checkModifier table, and it must stay in step with it.
 * Disabled and AlwaysActive both map to "no modifiers": neither names a key
 * combination, so neither has a mask, and callers gate on the enumerator
 * before asking.
 */
inline Qt::KeyboardModifiers modifierMaskFor(int modifierSetting)
{
    switch (modifierSetting) {
    case 1:
        return Qt::ShiftModifier;
    case 2:
        return Qt::ControlModifier;
    case 3:
        return Qt::AltModifier;
    case 4:
        return Qt::MetaModifier;
    case 5:
        return Qt::ControlModifier | Qt::AltModifier;
    case 6:
        return Qt::ControlModifier | Qt::ShiftModifier;
    case 7:
        return Qt::AltModifier | Qt::ShiftModifier;
    case 9:
        return Qt::AltModifier | Qt::MetaModifier;
    case 10:
        return Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    case 11:
        return Qt::MetaModifier | Qt::ShiftModifier;
    case 12:
        return Qt::ControlModifier | Qt::MetaModifier;
    default:
        return Qt::NoModifier;
    }
}

/**
 * @brief Whether @p mods is EXACTLY the combination @p modifierSetting names.
 *
 * checkModifier is deliberately subset-matching: a drag with Meta+Shift held
 * satisfies a Meta trigger, because holding an extra key should not cancel an
 * in-progress drag gesture. Wheel chords need the opposite. Two scroll keys
 * that differ only by one extra modifier (the stock Meta / Meta+Shift pair)
 * are DISTINCT verbs, and under subset matching the shorter one would swallow
 * every event meant for the longer, making the longer binding unreachable.
 * Extra modifiers therefore mean "not this chord" here.
 *
 * Only the four chord modifiers are compared. Keypad, group-switch and lock
 * modifiers ride along on real events and are not bindable, so folding them
 * into the comparison would make a chord fail under Num Lock.
 */
inline bool exactModifierMatch(int modifierSetting, Qt::KeyboardModifiers mods)
{
    constexpr Qt::KeyboardModifiers chordMask =
        Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    return (mods & chordMask) == modifierMaskFor(modifierSetting);
}

/**
 * @brief Whether any trigger matches the current state exactly on MODIFIERS.
 *
 * The strict peer of anyTriggerHeld, for consumers whose chords must not
 * shadow one another (the scrolling wheel chords). Nothing distinguishes
 * these triggers from drag triggers in storage — a trigger list is a trigger
 * list — so the caller decides which matcher a list is read with, and it
 * decides that by which SETTING the list came from.
 *
 * Exact on modifiers, SUBSET on buttons, and the asymmetry is deliberate
 * rather than an oversight: a trigger naming no button matches with any
 * buttons held, so wheeling while a button happens to be down still works.
 * The cost is that the shadowing this matcher prevents on the modifier axis
 * survives on the button axis — a modifier-only chord swallows events meant
 * for the same modifier plus a button. Callers that care should author
 * modifier-only or button-only chords, never combined — which is the shape
 * the scroll-key capture UI enforces, so the shadowing pair takes a
 * hand-edited config.
 *
 * DragModifier::AlwaysActive does NOT survive this matcher's semantics:
 * modifierMaskFor has no case for it, so it folds to Qt::NoModifier and the
 * entry comes to mean "only when nothing is held". Lists read with this
 * matcher are validated by canonicalWheelTriggerList, which drops it.
 *
 * A trigger with neither modifier nor button is skipped here as it is in
 * anyTriggerHeld: an all-zero entry is malformed config, and honouring it
 * would hand the consumer every unmodified event in the session.
 */
inline bool anyTriggerHeldExact(const QVector<ParsedTrigger>& triggers, Qt::KeyboardModifiers mods,
                                Qt::MouseButtons mouseButtons)
{
    for (const auto& t : triggers) {
        if (t.modifier == 0 && t.mouseButton == 0) {
            continue;
        }
        const bool btnMatch = (t.mouseButton == 0) || (static_cast<int>(mouseButtons) & t.mouseButton) != 0;
        if (exactModifierMatch(t.modifier, mods) && btnMatch) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check if any parsed trigger is currently held
 *
 * @param triggers Pre-parsed trigger list
 * @param mods Current keyboard modifier state
 * @param mouseButtons Current mouse button state
 * @return true if any trigger matches
 */
inline bool anyTriggerHeld(const QVector<ParsedTrigger>& triggers, Qt::KeyboardModifiers mods,
                           Qt::MouseButtons mouseButtons)
{
    for (const auto& t : triggers) {
        const bool modMatch = (t.modifier == 0) || checkModifier(t.modifier, mods);
        const bool btnMatch = (t.mouseButton == 0) || (static_cast<int>(mouseButtons) & t.mouseButton) != 0;
        if (modMatch && btnMatch && (t.modifier != 0 || t.mouseButton != 0))
            return true;
    }
    return false;
}

/**
 * @brief Parse trigger list from D-Bus QVariantList (handles QDBusArgument wrapping)
 *
 * D-Bus may deliver QVariantList-of-QVariantMap as QDBusArgument.
 * This function handles both wrapped and unwrapped forms.
 *
 * @param triggerVariant The raw QVariant from D-Bus
 * @param modifierFieldName Config key for the modifier field
 * @param mouseButtonFieldName Config key for the mouse button field
 * @return Vector of parsed triggers
 */
PHOSPHORCOMPOSITOR_EXPORT QVector<ParsedTrigger>
parseTriggers(const QVariant& triggerVariant, const QString& modifierFieldName, const QString& mouseButtonFieldName);

} // namespace TriggerParser
} // namespace PhosphorCompositor
