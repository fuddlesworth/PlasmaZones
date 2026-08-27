// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "configdefaults_scrolling_shortcuts.h"

namespace PlasmaZones {

// Chain link 9: dynamic per-monitor workspaces (Workspaces.*). The feature is
// a global layer below the three placement modes — the defaults here gate the
// daemon's WorkspaceController and the ownership-map lifecycle, not any one
// engine. Every accessor reaches call sites through the ConfigDefaults leaf.
class ConfigDefaultsWorkspaces : public ConfigDefaultsScrollingShortcuts
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Workspaces.Behavior
    // ═══════════════════════════════════════════════════════════════════════════

    /// Master opt-in for dynamic per-monitor workspaces. Off = current
    /// behavior, byte-identical (the WorkspaceController is not constructed).
    static constexpr bool workspacesEnabled()
    {
        return false;
    }

    /// Consent latch for writing KWin's PerOutputVirtualDesktops key on the
    /// user's behalf. Never written silently; recorded when the user accepts
    /// the inline confirmation on the Workspaces settings page.
    static constexpr bool workspacesManageKWinPerOutput()
    {
        return false;
    }
};

} // namespace PlasmaZones
