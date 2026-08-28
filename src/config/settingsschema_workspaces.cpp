// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The workspaces half of the settings schema: the Workspaces.Behavior gate
// scalars, the Workspaces.Named declaration list and its canonicalizing
// validator, and the Workspaces.Slots target family. Split out of
// settingsschema.cpp for file-size; the single entry point
// (appendWorkspacesSchema) is declared alongside every other appendXxxSchema
// in settingsschema.h.

#include "settingsschema.h"

#include "configdefaults.h"

#include <QSet>

namespace PlasmaZones {

namespace {

/// Canonicalize the named-workspace declaration list, the same
/// drop-malformed contract canonicalTriggerList applies to trigger lists: the
/// D-Bus boundary validates what it is handed, but the DISK path does not go
/// through it, so a hand-edited config reaches the daemon's declaration
/// applier unchecked. An entry that is not a map, or whose name is empty
/// after trimming, or whose name repeats one already accepted, is dropped —
/// duplicates because `name` is the identity the focus / move / route verbs
/// resolve against, and two entries claiming one name make which slice a verb
/// reaches depend on list order.
///
/// Surviving entries are rewritten to exactly the five declared fields, so an
/// unknown field a future (or hand-edited) config carries does not persist
/// through a save cycle.
QVariant canonicalNamedEntries(const QVariant& v)
{
    using CD = ConfigDefaults;
    const QVariantList raw = v.toList();
    QVariantList out;
    out.reserve(raw.size());
    QSet<QString> seenNames;
    for (const QVariant& entry : raw) {
        if (entry.typeId() != QMetaType::QVariantMap) {
            continue;
        }
        const QVariantMap src = entry.toMap();
        const QString name = src.value(CD::namedEntryNameField()).toString().trimmed();
        if (name.isEmpty() || seenNames.contains(name)) {
            continue;
        }
        seenNames.insert(name);
        QVariantMap canon;
        canon[CD::namedEntryNameField()] = name;
        canon[CD::namedEntryOutputField()] = src.value(CD::namedEntryOutputField()).toString();
        // Position is the preferred slice index; -1 means "before the trailing
        // empty". A non-numeric or negative-beyond--1 value falls back to that
        // sentinel rather than persisting an index no reconciler can honour.
        bool posOk = false;
        const int position = src.value(CD::namedEntryPositionField(), -1).toInt(&posOk);
        canon[CD::namedEntryPositionField()] = (posOk && position >= 0) ? position : -1;
        canon[CD::namedEntryFocusShortcutField()] = src.value(CD::namedEntryFocusShortcutField()).toString();
        canon[CD::namedEntryMoveShortcutField()] = src.value(CD::namedEntryMoveShortcutField()).toString();
        out.append(canon);
    }
    return out;
}

} // namespace

void appendWorkspacesSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    // Dynamic per-monitor workspaces: the gate scalars, plus the two consent /
    // takeover latches the enable flow writes.
    schema.groups[CD::workspacesBehaviorGroup()] = {
        {CD::enabledKey(), CD::workspacesEnabled(), QMetaType::Bool},
        {CD::manageKWinPerOutputKey(), CD::workspacesManageKWinPerOutput(), QMetaType::Bool},
        {CD::snapBackOsdHintKey(), CD::workspacesSnapBackOsdHint(), QMetaType::Bool},
        {CD::rebindKWinShortcutsKey(), CD::workspacesRebindKWinShortcuts(), QMetaType::Bool},
    };
    schema.groups[CD::workspacesNamedGroup()] = {
        {CD::entriesKey(), CD::workspacesNamedEntries(), QMetaType::QVariantList, {}, canonicalNamedEntries},
    };
    // Quick-slot targets: the named workspace each move slot sends the
    // active window to; empty = unassigned (the slot's chord does nothing).
    auto& slots = schema.groups[CD::workspacesSlotsGroup()];
    slots.reserve(CD::WorkspaceSlotCount);
    for (int slot = 1; slot <= CD::WorkspaceSlotCount; ++slot) {
        slots.append({CD::workspaceSlotTargetKey(slot), CD::workspaceSlotTarget(), QMetaType::QString});
    }
}

} // namespace PlasmaZones
