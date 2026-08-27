// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/settings/settings_detail.h"
#include "config/configdefaults.h"

namespace PlasmaZones {

// ── Workspaces (PhosphorConfig::Store-backed) ───────────────────────────────
// Dynamic per-monitor workspaces. Two Phase-1 gate scalars; the named-
// workspace declarations join in Phase 3. Defaults live in
// configdefaults_workspaces.h; the schema group in settingsschema.cpp.

P_STORE_GET(bool, workspacesEnabled, workspacesBehaviorGroup, enabledKey, bool)
P_STORE_SET_BOOL(setWorkspacesEnabled, workspacesBehaviorGroup, enabledKey, workspacesEnabledChanged)

P_STORE_GET(bool, workspacesManageKWinPerOutput, workspacesBehaviorGroup, manageKWinPerOutputKey, bool)
P_STORE_SET_BOOL(setWorkspacesManageKWinPerOutput, workspacesBehaviorGroup, manageKWinPerOutputKey,
                 workspacesManageKWinPerOutputChanged)

P_STORE_GET(bool, workspacesSnapBackOsdHint, workspacesBehaviorGroup, snapBackOsdHintKey, bool)
P_STORE_SET_BOOL(setWorkspacesSnapBackOsdHint, workspacesBehaviorGroup, snapBackOsdHintKey,
                 workspacesSnapBackOsdHintChanged)

P_STORE_GET(bool, workspacesRebindKWinShortcuts, workspacesBehaviorGroup, rebindKWinShortcutsKey, bool)
P_STORE_SET_BOOL(setWorkspacesRebindKWinShortcuts, workspacesBehaviorGroup, rebindKWinShortcutsKey,
                 workspacesRebindKWinShortcutsChanged)

// Named-workspace declarations: whole-replace QVariantList composite, same
// write shape as the trigger lists (read-back compare so a semantically
// identical list swallows no signal).
QVariantList Settings::workspacesNamedEntries() const
{
    return m_store->readVariant(ConfigDefaults::workspacesNamedGroup(), ConfigDefaults::entriesKey()).toList();
}

void Settings::setWorkspacesNamedEntries(const QVariantList& entries)
{
    const QVariantList before =
        m_store->readVariant(ConfigDefaults::workspacesNamedGroup(), ConfigDefaults::entriesKey()).toList();
    m_store->write(ConfigDefaults::workspacesNamedGroup(), ConfigDefaults::entriesKey(), entries);
    const QVariantList after =
        m_store->readVariant(ConfigDefaults::workspacesNamedGroup(), ConfigDefaults::entriesKey()).toList();
    if (before == after) {
        return;
    }
    Q_EMIT workspacesNamedEntriesChanged();
    Q_EMIT settingsChanged();
}

// ── Verb shortcuts (Shortcuts.Global leaves, like every other chord) ────────

P_STORE_GET(QString, workspaceFocusUpShortcut, shortcutsGlobalGroup, workspaceFocusUpKey, QString)
P_STORE_SET_STRING(setWorkspaceFocusUpShortcut, shortcutsGlobalGroup, workspaceFocusUpKey,
                   workspaceFocusUpShortcutChanged)
P_STORE_GET(QString, workspaceFocusDownShortcut, shortcutsGlobalGroup, workspaceFocusDownKey, QString)
P_STORE_SET_STRING(setWorkspaceFocusDownShortcut, shortcutsGlobalGroup, workspaceFocusDownKey,
                   workspaceFocusDownShortcutChanged)
P_STORE_GET(QString, workspaceMoveWindowUpShortcut, shortcutsGlobalGroup, workspaceMoveWindowUpKey, QString)
P_STORE_SET_STRING(setWorkspaceMoveWindowUpShortcut, shortcutsGlobalGroup, workspaceMoveWindowUpKey,
                   workspaceMoveWindowUpShortcutChanged)
P_STORE_GET(QString, workspaceMoveWindowDownShortcut, shortcutsGlobalGroup, workspaceMoveWindowDownKey, QString)
P_STORE_SET_STRING(setWorkspaceMoveWindowDownShortcut, shortcutsGlobalGroup, workspaceMoveWindowDownKey,
                   workspaceMoveWindowDownShortcutChanged)
P_STORE_GET(QString, workspaceMoveColumnUpShortcut, shortcutsGlobalGroup, workspaceMoveColumnUpKey, QString)
P_STORE_SET_STRING(setWorkspaceMoveColumnUpShortcut, shortcutsGlobalGroup, workspaceMoveColumnUpKey,
                   workspaceMoveColumnUpShortcutChanged)
P_STORE_GET(QString, workspaceMoveColumnDownShortcut, shortcutsGlobalGroup, workspaceMoveColumnDownKey, QString)
P_STORE_SET_STRING(setWorkspaceMoveColumnDownShortcut, shortcutsGlobalGroup, workspaceMoveColumnDownKey,
                   workspaceMoveColumnDownShortcutChanged)
P_STORE_GET(QString, workspaceReorderUpShortcut, shortcutsGlobalGroup, workspaceReorderUpKey, QString)
P_STORE_SET_STRING(setWorkspaceReorderUpShortcut, shortcutsGlobalGroup, workspaceReorderUpKey,
                   workspaceReorderUpShortcutChanged)
P_STORE_GET(QString, workspaceReorderDownShortcut, shortcutsGlobalGroup, workspaceReorderDownKey, QString)
P_STORE_SET_STRING(setWorkspaceReorderDownShortcut, shortcutsGlobalGroup, workspaceReorderDownKey,
                   workspaceReorderDownShortcutChanged)
P_STORE_GET(QString, workspaceMoveToMonitorLeftShortcut, shortcutsGlobalGroup, workspaceMoveToMonitorLeftKey, QString)
P_STORE_SET_STRING(setWorkspaceMoveToMonitorLeftShortcut, shortcutsGlobalGroup, workspaceMoveToMonitorLeftKey,
                   workspaceMoveToMonitorLeftShortcutChanged)
P_STORE_GET(QString, workspaceMoveToMonitorRightShortcut, shortcutsGlobalGroup, workspaceMoveToMonitorRightKey, QString)
P_STORE_SET_STRING(setWorkspaceMoveToMonitorRightShortcut, shortcutsGlobalGroup, workspaceMoveToMonitorRightKey,
                   workspaceMoveToMonitorRightShortcutChanged)

// ── Indexed workspace slots ─────────────────────────────────────────────────
// Nine focus + nine move-window slots, unset by default (the user binds the
// ones they use — the quick-layout-slot convention). One shared NOTIFY for
// both families: the shortcuts page re-reads all fields on it, and the
// ShortcutManager's rebind rides settingsChanged regardless.

namespace {
inline constexpr int WorkspaceSlotCount = 9;
}

QString Settings::workspaceSlotTarget(int index) const
{
    if (index < 0 || index >= WorkspaceSlotCount) {
        return {};
    }
    return m_store->read<QString>(ConfigDefaults::workspacesSlotsGroup(),
                                  ConfigDefaults::workspaceSlotTargetKey(index + 1));
}

void Settings::setWorkspaceSlotTarget(int index, const QString& workspaceName)
{
    if (index < 0 || index >= WorkspaceSlotCount) {
        return;
    }
    const QString key = ConfigDefaults::workspaceSlotTargetKey(index + 1);
    if (m_store->read<QString>(ConfigDefaults::workspacesSlotsGroup(), key) == workspaceName) {
        return;
    }
    m_store->write(ConfigDefaults::workspacesSlotsGroup(), key, workspaceName);
    Q_EMIT workspaceSlotTargetsChanged();
    Q_EMIT settingsChanged();
}

QString Settings::workspaceFocusSlotShortcut(int index) const
{
    if (index < 0 || index >= WorkspaceSlotCount) {
        return {};
    }
    return m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(),
                                  ConfigDefaults::workspaceFocusSlotKey(index + 1));
}

void Settings::setWorkspaceFocusSlotShortcut(int index, const QString& shortcut)
{
    if (index < 0 || index >= WorkspaceSlotCount) {
        return;
    }
    const QString key = ConfigDefaults::workspaceFocusSlotKey(index + 1);
    if (m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), key) == shortcut) {
        return;
    }
    m_store->write(ConfigDefaults::shortcutsGlobalGroup(), key, shortcut);
    Q_EMIT workspaceSlotShortcutsChanged();
    Q_EMIT settingsChanged();
}

QString Settings::workspaceMoveSlotShortcut(int index) const
{
    if (index < 0 || index >= WorkspaceSlotCount) {
        return {};
    }
    return m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(),
                                  ConfigDefaults::workspaceMoveSlotKey(index + 1));
}

void Settings::setWorkspaceMoveSlotShortcut(int index, const QString& shortcut)
{
    if (index < 0 || index >= WorkspaceSlotCount) {
        return;
    }
    const QString key = ConfigDefaults::workspaceMoveSlotKey(index + 1);
    if (m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), key) == shortcut) {
        return;
    }
    m_store->write(ConfigDefaults::shortcutsGlobalGroup(), key, shortcut);
    Q_EMIT workspaceSlotShortcutsChanged();
    Q_EMIT settingsChanged();
}

} // namespace PlasmaZones
