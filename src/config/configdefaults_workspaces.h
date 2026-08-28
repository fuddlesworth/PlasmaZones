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
    // Down/Up" chords (user decision, 2026-08-26). That takeover is real: the
    // daemon's stock-rebind pass (src/daemon/daemon/workspaces.cpp) backs up
    // and clears exactly the "Switch One Desktop *" / "Walk Through Desktops"
    // actions while the feature and the rebind toggle are both on, and
    // restores them on disable.
    //
    // Nothing else here may sit on a stock KWin chord, because that table
    // covers only the SWITCH family. In particular Meta+Ctrl+Shift+Arrow is
    // KWin's stock "Window One Desktop <direction>" quad, which the takeover
    // does NOT release — a default there would stay permanently uncaptured
    // (KGlobalAccel keeps the chord with KWin) and read to the user as a verb
    // that silently does nothing. Every remaining Meta+Arrow tier is already
    // claimed by another PlasmaZones family (moveWindow Meta+Alt+Shift,
    // swapWindow and swapVirtualScreen Meta+Ctrl+Alt and
    // Meta+Ctrl+Alt+Shift, span Ctrl+Alt, focusZone Alt+Shift — see the
    // table in configdefaults.h, and the collision guard in
    // tests/unit/config/settings/test_scrolling_settings.cpp).
    //
    // So the move-window pair and the move-to-monitor pair ship UNBOUND, the
    // same way the nine focus slots do. An unbound verb is honest: the user
    // assigns a chord that works, instead of a factory default that stock
    // KWin or a sibling family would swallow.
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
        return QString();
    }
    static QString workspaceMoveWindowDownShortcut()
    {
        return QString();
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
    // The reorder pair above keeps Meta+Ctrl+Shift+PgUp/PgDown: the stock
    // window-to-desktop quad is arrow-only, so the page keys on that tier are
    // free. These two are the arrow members of the family and move up a tier
    // for the reason the banner gives.
    static QString workspaceMoveToMonitorLeftShortcut()
    {
        return QString();
    }
    static QString workspaceMoveToMonitorRightShortcut()
    {
        return QString();
    }

    // Move-slot chords (quick-layout model: fixed factory chords, rebindable
    // in the Shortcuts KCM; the settings app assigns the TARGET workspace).
    // Meta+Shift+N is niri's own move-to-workspace-N family and is unclaimed
    // by every other PlasmaZones default (collision-checked in the daemon's
    // duplicate-defaults test).
    static QString workspaceMoveSlot1Shortcut()
    {
        return QStringLiteral("Meta+Shift+1");
    }
    static QString workspaceMoveSlot2Shortcut()
    {
        return QStringLiteral("Meta+Shift+2");
    }
    static QString workspaceMoveSlot3Shortcut()
    {
        return QStringLiteral("Meta+Shift+3");
    }
    static QString workspaceMoveSlot4Shortcut()
    {
        return QStringLiteral("Meta+Shift+4");
    }
    static QString workspaceMoveSlot5Shortcut()
    {
        return QStringLiteral("Meta+Shift+5");
    }
    static QString workspaceMoveSlot6Shortcut()
    {
        return QStringLiteral("Meta+Shift+6");
    }
    static QString workspaceMoveSlot7Shortcut()
    {
        return QStringLiteral("Meta+Shift+7");
    }
    static QString workspaceMoveSlot8Shortcut()
    {
        return QStringLiteral("Meta+Shift+8");
    }
    static QString workspaceMoveSlot9Shortcut()
    {
        return QStringLiteral("Meta+Shift+9");
    }

    /// Focus-slot chords: unset by default for every slot. The family is
    /// daemon-registered and bound by the user in KDE's Shortcuts settings,
    /// so there is no factory chord to collide with anything. One accessor
    /// rather than nine identical ones — the value does not vary by slot, and
    /// the schema loop reads it per slot.
    static QString workspaceFocusSlotShortcut()
    {
        return QString();
    }

    /// Quick-slot targets: unassigned by default. An empty target means the
    /// slot's chord resolves to no workspace and the verb does nothing.
    static QString workspaceSlotTarget()
    {
        return QString();
    }
};

} // namespace PlasmaZones
