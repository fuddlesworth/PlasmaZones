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
///
/// **Input MUST NOT contain the AlwaysActive sentinel** — the bridge is
/// lossy on `DragModifier::AlwaysActive` (modifier=8 → bitmask=0), so a
/// sentinel entry round-trips to a phantom "no-modifier, no-mouse-button"
/// chip in the QML widget. Call `stripAlwaysActiveTrigger` first on any
/// list that may carry the sentinel.
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

/// Apply an "Always active" master-toggle change to a stored trigger list.
/// Strips the sentinel from the current list, then either merges it back
/// (toggle on), falls back to @p factoryDefault (toggle off with no
/// non-sentinel triggers left), or returns the bare non-sentinel list
/// (toggle off with surviving user triggers). Centralises the logic shared
/// by SnappingBehaviorController::setAlwaysActivateOnDrag and
/// TilingBehaviorController::setAlwaysReinsertIntoStack — both wrap the
/// same sentinel-cap + fallback dance around the master-toggle bit.
QVariantList applyAlwaysActiveToggle(const QVariantList& currentStored, bool enabled,
                                     const QVariantList& factoryDefault);

/// Normalise a QML-authored explicit trigger edit: strip the sentinel (it
/// belongs to the master toggle), then re-merge it iff the master toggle
/// is currently on. Used by setDragActivationTriggers /
/// setAutotileDragInsertTriggers so a deselect-all in the widget does not
/// silently un-flip the master.
QVariantList normaliseExplicitEdit(const QVariantList& fromQml, bool masterToggleOn);

} // namespace PlasmaZones::TriggerUtils
