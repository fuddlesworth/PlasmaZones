// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modifierutils.h"

namespace PlasmaZones {
namespace ModifierUtils {

// Qt modifier flags (avoid Qt header dependency for simple constants)
namespace {
constexpr int ShiftModifier = 0x02000000;
constexpr int ControlModifier = 0x04000000;
constexpr int AltModifier = 0x08000000;
constexpr int MetaModifier = 0x10000000;
} // namespace

int dragModifierToBitmask(int enumValue)
{
    switch (enumValue) {
    case 0:
        return 0; // None
    case 1:
        return ShiftModifier; // Shift
    case 2:
        return ControlModifier; // Ctrl
    case 3:
        return AltModifier; // Alt
    case 4:
        return MetaModifier; // Meta
    case 5:
        return ControlModifier | AltModifier; // Ctrl+Alt
    case 6:
        return ControlModifier | ShiftModifier; // Ctrl+Shift
    case 7:
        return AltModifier | ShiftModifier; // Alt+Shift
    case 8:
        return 0; // AlwaysActive - no modifier keys in checkbox UI
    case 9:
        return AltModifier | MetaModifier; // Alt+Meta
    case 10:
        return ControlModifier | AltModifier | MetaModifier; // Ctrl+Alt+Meta
    case 11:
        return MetaModifier | ShiftModifier; // Meta+Shift
    case 12:
        return ControlModifier | MetaModifier; // Ctrl+Meta
    default:
        return 0;
    }
}

int bitmaskToDragModifier(int bitmask)
{
    if (bitmask == 0)
        return 0; // None

    bool hasShift = (bitmask & ShiftModifier) != 0;
    bool hasCtrl = (bitmask & ControlModifier) != 0;
    bool hasAlt = (bitmask & AltModifier) != 0;
    bool hasMeta = (bitmask & MetaModifier) != 0;

    // Single modifiers
    if (hasShift && !hasCtrl && !hasAlt && !hasMeta)
        return 1; // Shift
    if (hasCtrl && !hasShift && !hasAlt && !hasMeta)
        return 2; // Ctrl
    if (hasAlt && !hasCtrl && !hasShift && !hasMeta)
        return 3; // Alt
    if (hasMeta && !hasCtrl && !hasAlt && !hasShift)
        return 4; // Meta

    // Two- and three-modifier combinations
    if (hasMeta && hasShift && !hasCtrl && !hasAlt)
        return 11; // Meta+Shift
    if (hasCtrl && hasMeta && !hasAlt && !hasShift)
        return 12; // Ctrl+Meta
    if (hasAlt && hasMeta && !hasCtrl && !hasShift)
        return 9; // Alt+Meta
    if (hasCtrl && hasAlt && hasMeta && !hasShift)
        return 10; // Ctrl+Alt+Meta
    if (hasCtrl && hasAlt && !hasShift && !hasMeta)
        return 5; // Ctrl+Alt
    if (hasCtrl && hasShift && !hasAlt && !hasMeta)
        return 6; // Ctrl+Shift
    if (hasAlt && hasShift && !hasCtrl && !hasMeta)
        return 7; // Alt+Shift

    // For other combinations not in enum, map to closest match or default.
    // This allows UI flexibility while maintaining enum compatibility.
    //
    // ORDER MATTERS here, and adding an enumerator can silently re-point an
    // existing combination. The Meta+Shift arm below sits ahead of the
    // Alt+Shift one, so Meta+Alt+Shift now resolves to Meta+Shift where it
    // used to resolve to Alt+Shift before MetaShift existed; likewise
    // Ctrl+Meta+Shift moved off Ctrl+Shift. Both keep Meta rather than
    // dropping it, which is the better of two lossy answers, but the choice
    // is deliberate rather than incidental.
    //
    // Nothing here includes the DragModifier enum it mirrors — the switch is
    // written against bare wire values, so a new enumerator can never fail to
    // compile in this file. tests/unit/core/test_modifier_utils.cpp is what
    // ties the two together: it walks every enumerator up to MaxDragModifier
    // and pins this tail's order.
    if (hasCtrl && hasAlt && hasMeta)
        return 10; // Ctrl+Alt+Meta (e.g. all four modifiers)
    if (hasMeta && hasShift)
        return 11; // Meta+Shift (closest)
    if (hasCtrl && hasMeta)
        return 12; // Ctrl+Meta (closest)
    if (hasCtrl && hasAlt)
        return 5; // Ctrl+Alt (closest)
    if (hasCtrl && hasShift)
        return 6; // Ctrl+Shift (closest)
    if (hasAlt && hasShift)
        return 7; // Alt+Shift (closest)
    if (hasCtrl)
        return 2; // Default to Ctrl
    if (hasAlt)
        return 3; // Default to Alt
    if (hasShift)
        return 1; // Default to Shift
    if (hasMeta)
        return 4; // Default to Meta

    return 0; // None
}

} // namespace ModifierUtils
} // namespace PlasmaZones
