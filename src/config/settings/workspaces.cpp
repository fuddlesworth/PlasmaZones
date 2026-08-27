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

} // namespace PlasmaZones
