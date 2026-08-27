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

    /// OSD hint when owner-wins snap-back returns a screen to its own slice.
    static constexpr bool workspacesSnapBackOsdHint()
    {
        return true;
    }

    /// Neutralize KWin's stock "Switch One Desktop" chords while the feature
    /// is on (restored on disable). On by default: the stock chords iterate
    /// the GLOBAL desktop pool and would trip owner-wins snap-back on nearly
    /// every press, and the focus verbs' defaults deliberately reuse
    /// Meta+Ctrl+Up/Down (user decision, 2026-08-26).
    static constexpr bool workspacesRebindKWinShortcuts()
    {
        return true;
    }

    /// Named-workspace declarations: none by default. Entry shape (all
    /// QVariantMap fields): name (unique, non-empty), output (screenId, empty
    /// = unpinned), position (preferred slice index, -1 = before the trailing
    /// empty), focusShortcut / moveShortcut (chords, empty = unbound).
    static QVariantList workspacesNamedEntries()
    {
        return {};
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Workspace verb shortcuts (Shortcuts.Global)
    //
    // The focus pair deliberately takes over KWin's stock "Switch One Desktop
    // Down/Up" chords (user decision, 2026-08-26): until Phase 5's stock
    // rebinding releases them, KGlobalAccel keeps the chord with KWin and our
    // registration simply stays uncaptured — a conflict, not a breakage. The
    // Shift/Alt derivatives are unclaimed by both KWin stock and every other
    // PlasmaZones default (collision-checked against the whole table).
    // ═══════════════════════════════════════════════════════════════════════════

    static QString workspaceFocusUpShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Up");
    }
    static QString workspaceFocusDownShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Down");
    }
    static QString workspaceMoveWindowUpShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Shift+Up");
    }
    static QString workspaceMoveWindowDownShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Shift+Down");
    }
    static QString workspaceMoveColumnUpShortcut()
    {
        // NOT Meta+Ctrl+Alt+Arrow (the plan's first pick): that family is
        // the swap-window quad. PgUp/PgDown on the same modifier tier is
        // unclaimed and mirrors the reorder pair's PgUp/PgDown shape.
        return QStringLiteral("Meta+Ctrl+Alt+PgUp");
    }
    static QString workspaceMoveColumnDownShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Alt+PgDown");
    }
    static QString workspaceReorderUpShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Shift+PgUp");
    }
    static QString workspaceReorderDownShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Shift+PgDown");
    }
    static QString workspaceMoveToMonitorLeftShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Shift+Left");
    }
    static QString workspaceMoveToMonitorRightShortcut()
    {
        return QStringLiteral("Meta+Ctrl+Shift+Right");
    }
};

} // namespace PlasmaZones
