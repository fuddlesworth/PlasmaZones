// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusArgument>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <Qt>

namespace PlasmaZones {

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
 * Must stay in sync with WindowDragAdaptor::checkModifier() in the daemon.
 * The enum values are defined in src/core/interfaces.h (DragModifier).
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
    default:
        return false;
    }
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
QVector<ParsedTrigger> parseTriggers(const QVariant& triggerVariant, const QString& modifierFieldName,
                                     const QString& mouseButtonFieldName);

} // namespace TriggerParser
} // namespace PlasmaZones
