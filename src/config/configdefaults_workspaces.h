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

    // ── Workspaces.Overview ──────────────────────────────────────────────

    /// How far the overview zooms out when fully open: a cell is the screen
    /// scaled by this. niri's overview.zoom default and bounds.
    static constexpr double overviewZoom()
    {
        return 0.5;
    }
    static constexpr double overviewZoomMin()
    {
        return 0.1;
    }
    static constexpr double overviewZoomMax()
    {
        return 0.75;
    }

    /// The colour drawn behind the zoomed-out workspaces. A concrete colour
    /// rather than a theme-fallback sentinel: the overview replaces the
    /// whole screen and niri's neutral dark grey reads correctly on every
    /// wallpaper.
    static QString overviewBackdropColor()
    {
        return QStringLiteral("#262626");
    }

    /// Open the overview with a four-finger touchpad swipe up (three fingers
    /// on a touchscreen).
    static constexpr bool overviewGestureEnabled()
    {
        return true;
    }

    /// The mouse wheel over a monitor's column switches that monitor's
    /// workspace while the overview is open.
    static constexpr bool overviewWheelSwitchesWorkspaces()
    {
        return true;
    }

    /// Show each workspace's name (or its number) above its cell.
    static constexpr bool overviewShowWorkspaceNames()
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
    /// The overview toggle takes over KWin's stock Overview chord the same
    /// way the focus pair takes over the desktop-switch chords: the stock
    /// rebind pass backs up and clears KWin's "Overview" action while the
    /// feature and the rebind toggle are on, and restores it on disable.
    static QString overviewToggleShortcut()
    {
        return QStringLiteral("Meta+W");
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
    // free. The two move-to-monitor verbs below ship UNBOUND: their natural
    // chord, Meta+Ctrl+Shift+Arrow, is stock KWin's "Window One Desktop"
    // family, which the takeover does not release. The user binds them.
    static QString workspaceMoveToMonitorLeftShortcut()
    {
        return QString();
    }
    static QString workspaceMoveToMonitorRightShortcut()
    {
        return QString();
    }

    /// Move-slot chords: unset by default for every slot, matching the
    /// focus-slot family below. A slot's target workspace is unassigned until
    /// the user picks one in the settings app, so a factory chord here would
    /// claim a global binding that resolves to nothing on a fresh install.
    /// The user assigns both halves, the target in the settings app and the
    /// chord in KDE's Shortcuts settings. One accessor rather than nine
    /// identical ones, since the value does not vary by slot.
    static QString workspaceMoveSlotShortcut()
    {
        return QString();
    }

    /// Focus-slot chords: unset by default for every slot, for the same
    /// reason as the move slots above. The family is daemon-registered and
    /// bound by the user in KDE's Shortcuts settings. One accessor
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
