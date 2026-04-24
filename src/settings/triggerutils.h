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

} // namespace PlasmaZones::TriggerUtils
