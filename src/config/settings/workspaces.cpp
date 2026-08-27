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

} // namespace PlasmaZones
