// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QVariantList>

namespace PlasmaZones::TriggerUtils {

/// Convert a list of stored triggers (DragModifier enum values) into the
/// bitmask form QML widgets expect. Each trigger is a map with
/// `modifier` (int) + `mouseButton` (int) keys.
///
/// Storage stores the modifier as a `DragModifier` enum value; QML reads
/// it as a Qt::KeyboardModifier-style bitmask. The two are bridged by
/// `ModifierUtils::dragModifierToBitmask`.
QVariantList convertTriggersForQml(const QVariantList& triggers);

/// Inverse of convertTriggersForQml — QML-authored triggers come in as
/// Qt-modifier bitmasks; convert them back to `DragModifier` enum values
/// for persistence.
QVariantList convertTriggersForStorage(const QVariantList& triggers);

/// Returns true if any trigger in the list has the `DragModifier::AlwaysActive`
/// modifier. Used to expose the "drag is always active" / "autotile reinsert
/// is always active" booleans to QML.
bool hasAlwaysActiveTrigger(const QVariantList& triggers);

/// Build a single-element trigger list representing the "always active"
/// state. Written to storage when the "Always active" toggle is turned on.
QVariantList makeAlwaysActiveTriggerList();

/// Drop every entry whose modifier is the AlwaysActive sentinel. Operates on
/// storage-form (DragModifier enum) lists. The trigger widget never displays
/// AlwaysActive — the master "Activate on every drag" toggle owns that bit
/// — so any QML-facing surface and any re-merge on write strips first.
QVariantList stripAlwaysActiveTrigger(const QVariantList& triggers);

/// Prepend the AlwaysActive sentinel to a list of user-configured triggers
/// and trim the user portion so the combined list fits within
/// `ConfigDefaults::maxTriggersPerAction()`. Sentinel goes at the front so
/// it survives `Settings::writeTriggerList`'s `.mid(0, MAX)` cap; if the
/// user already has MAX entries, the trailing one is dropped (visible as a
/// chip disappearing) instead of the sentinel being silently truncated —
/// which would make the always-active toggle quietly fail. Input must NOT
/// contain the sentinel; call `stripAlwaysActiveTrigger` first if unsure.
QVariantList mergeAlwaysActiveTrigger(const QVariantList& nonSentinelTriggers);

} // namespace PlasmaZones::TriggerUtils
