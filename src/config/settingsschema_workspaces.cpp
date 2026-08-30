// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The workspaces half of the settings schema: the Workspaces.Behavior gate
// scalars, the Workspaces.Named declaration list and its canonicalizing
// validator, and the Workspaces.Slots target family. Split out of
// settingsschema.cpp for file-size, along with the workspace rows of the
// Shortcuts.Global list. Both entry points (appendWorkspacesSchema and
// appendWorkspacesShortcutKeys) are declared alongside every other
// appendXxxSchema in settingsschema.h.

#include "settingsschema.h"

#include "configdefaults.h"

#include <QSet>

#include <algorithm>

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
        // The name leaves this process: the daemon uses it as the objectName
        // and description of an ad-hoc KGlobalAccel action, and KGlobalAccel
        // persists both into kglobalshortcutsrc. A hand-edited config is the
        // one path that reaches here unvalidated (the D-Bus boundary checks
        // its own callers), so bound the length and drop anything carrying a
        // control or formatting character, which an INI file cannot round-trip
        // and no real workspace name contains.
        if (name.size() > CD::WorkspaceNameMaxLength) {
            continue;
        }
        const bool printable = std::all_of(name.cbegin(), name.cend(), [](QChar c) {
            return c.isPrint();
        });
        if (!printable) {
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
        {CD::enabledKey(), CD::workspacesEnabled(), QMetaType::Bool,
         QStringLiteral("Gives every monitor its own list of workspaces instead of one global set shared by all "
                        "screens. Requires KWin's per-output virtual desktops.")},
        {CD::manageKWinPerOutputKey(), CD::workspacesManageKWinPerOutput(), QMetaType::Bool,
         QStringLiteral("Records that the user consented to PlasmaZones writing KWin's PerOutputVirtualDesktops key. "
                        "The key is never written without this, and never reverted when the feature is disabled.")},
        {CD::snapBackOsdHintKey(), CD::workspacesSnapBackOsdHint(), QMetaType::Bool,
         QStringLiteral("Shows an OSD hint when switching to another monitor's workspace through the Pager or the "
                        "Overview snaps back to the owning monitor.")},
        {CD::rebindKWinShortcutsKey(), CD::workspacesRebindKWinShortcuts(), QMetaType::Bool,
         QStringLiteral("Takes over KWin's stock Switch One Desktop shortcuts while the feature is on, since they "
                        "walk the whole shared desktop pool rather than one monitor's list. Restored on disable.")},
    };
    schema.groups[CD::workspacesNamedGroup()] = {
        {CD::entriesKey(), CD::workspacesNamedEntries(), QMetaType::QVariantList,
         QStringLiteral("The named workspaces, each of which persists while empty and may be pinned to a monitor."),
         canonicalNamedEntries},
    };
    // Quick-slot targets: the named workspace each move slot sends the
    // active window to; empty = unassigned (the slot's chord does nothing).
    auto& slots = schema.groups[CD::workspacesSlotsGroup()];
    slots.reserve(CD::WorkspaceSlotCount);
    for (int slot = 1; slot <= CD::WorkspaceSlotCount; ++slot) {
        slots.append(
            {CD::workspaceSlotTargetKey(slot), CD::workspaceSlotTarget(), QMetaType::QString,
             QStringLiteral("The named workspace quick slot %1 targets. Empty leaves the slot unassigned.").arg(slot)});
    }
}

/// The Shortcuts.Global rows for the dynamic-workspaces verbs and quick
/// slots. Declared in settingsschema.h and called from appendShortcutsSchema
/// while it builds that group's list.
void appendWorkspacesShortcutKeys(QVector<PhosphorConfig::KeyDef>& globals)
{
    using CD = ConfigDefaults;
    const auto addShortcut = [](QVector<PhosphorConfig::KeyDef>& list, const QString& key, const QString& defaultValue,
                                const QString& description) {
        list.append({key, defaultValue, QMetaType::QString, description});
    };
    addShortcut(globals, CD::workspaceFocusUpKey(), CD::workspaceFocusUpShortcut(),
                QStringLiteral("Switches this monitor to the workspace above its current one."));
    addShortcut(globals, CD::workspaceFocusDownKey(), CD::workspaceFocusDownShortcut(),
                QStringLiteral("Switches this monitor to the workspace below its current one."));
    addShortcut(globals, CD::workspaceMoveWindowUpKey(), CD::workspaceMoveWindowUpShortcut(),
                QStringLiteral("Moves the focused window to the workspace above this monitor's current one."));
    addShortcut(globals, CD::workspaceMoveWindowDownKey(), CD::workspaceMoveWindowDownShortcut(),
                QStringLiteral("Moves the focused window to the workspace below this monitor's current one."));
    addShortcut(globals, CD::workspaceMoveColumnUpKey(), CD::workspaceMoveColumnUpShortcut(),
                QStringLiteral("Moves the focused window's whole scrolling column to the workspace above."));
    addShortcut(globals, CD::workspaceMoveColumnDownKey(), CD::workspaceMoveColumnDownShortcut(),
                QStringLiteral("Moves the focused window's whole scrolling column to the workspace below."));
    addShortcut(globals, CD::workspaceReorderUpKey(), CD::workspaceReorderUpShortcut(),
                QStringLiteral("Moves the current workspace one place earlier in this monitor's list."));
    addShortcut(globals, CD::workspaceReorderDownKey(), CD::workspaceReorderDownShortcut(),
                QStringLiteral("Moves the current workspace one place later in this monitor's list."));
    addShortcut(globals, CD::workspaceMoveToMonitorLeftKey(), CD::workspaceMoveToMonitorLeftShortcut(),
                QStringLiteral("Hands the current workspace to the monitor to the left."));
    addShortcut(globals, CD::workspaceMoveToMonitorRightKey(), CD::workspaceMoveToMonitorRightShortcut(),
                QStringLiteral("Hands the current workspace to the monitor to the right."));
    // Workspace quick-shortcut slots. Both families ship unbound: a slot's
    // target workspace is unassigned until the user picks one, so a factory
    // chord would claim a global binding that resolves to nothing on a fresh
    // install. Both are daemon-registered, so the user assigns the chords in
    // KDE's Shortcuts settings and the targets in the app.
    for (int slot = 1; slot <= CD::WorkspaceSlotCount; ++slot) {
        addShortcut(globals, CD::workspaceMoveSlotKey(slot), CD::workspaceMoveSlotShortcut(),
                    QStringLiteral("Moves the focused window to the named workspace assigned to quick slot %1. "
                                   "Unbound by default.")
                        .arg(slot));
        // Positional, unlike the move slot above: this switches the acting
        // monitor to the Nth workspace of its own list. It does not read the
        // Workspaces.Slots target assignments.
        addShortcut(
            globals, CD::workspaceFocusSlotKey(slot), CD::workspaceFocusSlotShortcut(),
            QStringLiteral("Switches this monitor to workspace %1 of its own list. Unbound by default.").arg(slot));
    }
}

} // namespace PlasmaZones
