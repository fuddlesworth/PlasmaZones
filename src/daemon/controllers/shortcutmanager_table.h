// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <cstddef>

namespace PlasmaZones {

class Settings;
class ShortcutManager;

/// The static (settings-driven, non-indexed) shortcut registration table.
/// Defined in shortcutmanager_table.cpp; walked by ShortcutManager's
/// buildEntries() and staticShortcutIds(). The indexed slot families
/// (quick_layout_N, snap_to_zone_N, workspace_move_slot_N,
/// workspace_focus_slot_N) are NOT here — their getters are array-indexed, so
/// the manager registers them in its own loops.
namespace ShortcutTable {

struct StaticEntry
{
    const char* id;
    QString (*defGetter)();
    QString (Settings::*curGetter)() const;
    const char* label;
    /// Capture-less so it decays to a function pointer; reaches the manager
    /// only through this argument.
    void (*fire)(ShortcutManager*);
};

/// Pointer to the first row; `staticEntryCount()` rows follow contiguously.
const StaticEntry* staticEntries();
std::size_t staticEntryCount();

} // namespace ShortcutTable
} // namespace PlasmaZones
