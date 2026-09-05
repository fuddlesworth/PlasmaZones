// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "configkeys_scrolling.h"

// configkeys_scrolling.h above undefines the accessor macros at its end, so
// pull them back in for this link's own declarations and undefine them again
// below.
#include "configkeymacro.h"

namespace PlasmaZones {

/**
 * @brief The Workspaces.* group names, key strings and named-entry field names.
 *
 * A link in the ConfigKeys → … → ConfigDefaults inheritance chain (the same
 * shape configkeys_scrolling.h uses), split out of configkeys.h by concern
 * when that file hit its size ceiling. Call sites keep addressing everything
 * through ConfigDefaults::.
 *
 * Dynamic per-monitor workspaces are mode-neutral: the feature is a layer
 * below all three placement modes, so the groups sit at the top level rather
 * than under Snapping.* / Tiling.* / Scrolling.*.
 */
class ConfigKeysWorkspaces : public ConfigKeysScrolling
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Config Groups — Workspaces.*
    // ═══════════════════════════════════════════════════════════════════════════

    // The top-level container. Not a group anything reads or writes directly:
    // it exists so reset() can drop the whole Workspaces subtree in one
    // deleteGroup, the way the "Snapping" and "Shortcuts" rows do for theirs.
    P_CONFIG_GROUP(workspacesGroup, "Workspaces")
    P_CONFIG_GROUP(workspacesBehaviorGroup, "Workspaces.Behavior")
    P_CONFIG_GROUP(workspacesNamedGroup, "Workspaces.Named")
    P_CONFIG_GROUP(workspacesSlotsGroup, "Workspaces.Slots")
    // The workspace overview's look and input (read by the overview KWin
    // effect through the settings wire, so every key here is registered in
    // the settings adaptor registry).
    P_CONFIG_GROUP(workspacesOverviewGroup, "Workspaces.Overview")

    // ═══════════════════════════════════════════════════════════════════════════
    // Indexed quick-slot count
    //
    // The one spelling of "nine" behind the slot families: the Shortcuts.Global
    // WorkspaceMoveSlotN / WorkspaceFocusSlotN chords, the Workspaces.Slots
    // TargetN entries, the key-builder range guards below, the schema's
    // declaration loops and their defaults arrays, and Settings' index bounds.
    // A raised count must move all of them together, so they all read it here.
    // ═══════════════════════════════════════════════════════════════════════════

    static constexpr int WorkspaceSlotCount = 9;

    /// Longest accepted named-workspace name. A name is not just display
    /// text: the daemon hands it to KGlobalAccel as an ad-hoc action's
    /// objectName and description, and KGlobalAccel writes that verbatim into
    /// kglobalshortcutsrc, an INI file. Anything a person would actually type
    /// as a workspace name fits well inside this, and the cap keeps a
    /// hand-edited config from pushing an unbounded string into another
    /// program's state file.
    static constexpr int WorkspaceNameMaxLength = 64;

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Workspaces.Behavior
    // ═══════════════════════════════════════════════════════════════════════════

    // Consent latch for writing KWin's PerOutputVirtualDesktops kwinrc key on
    // the user's behalf (dynamic workspaces enable flow).
    P_CONFIG_KEY(manageKWinPerOutputKey, "ManageKWinPerOutput")
    // Owner-wins snap-back OSD hint toggle.
    P_CONFIG_KEY(snapBackOsdHintKey, "SnapBackOsdHint")
    // Take over KWin's stock desktop-switch chords while the feature is on.
    P_CONFIG_KEY(rebindKWinShortcutsKey, "RebindKWinDesktopShortcuts")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Workspaces.Named
    // ═══════════════════════════════════════════════════════════════════════════

    // Named-workspace declarations: JSON array of
    // {name, output, position, focusShortcut, moveShortcut} maps.
    P_CONFIG_KEY(entriesKey, "Entries")

    // The five field names inside one declaration map. Routed through
    // accessors for the same reason every other config string is: the daemon
    // reads them to bind the per-name chords and the settings app writes them,
    // so a rename in one file must not leave the other reading a field that no
    // longer exists.
    P_CONFIG_KEY(namedEntryNameField, "name")
    P_CONFIG_KEY(namedEntryOutputField, "output")
    P_CONFIG_KEY(namedEntryPositionField, "position")
    P_CONFIG_KEY(namedEntryFocusShortcutField, "focusShortcut")
    P_CONFIG_KEY(namedEntryMoveShortcutField, "moveShortcut")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — indexed workspace slots
    //
    // The two chord families are Shortcuts.Global leaves (same builder shape
    // and range contract as quickLayoutKey); the target family lives under
    // Workspaces.Slots. All three are bounded by WorkspaceSlotCount above.
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(workspaceMoveSlotKeyPattern, "WorkspaceMoveSlot%1")
    static QString workspaceMoveSlotKey(int n)
    {
        // qFatal, like quickLayoutKey: an out-of-range value would otherwise
        // round-trip as "WorkspaceMoveSlot100" and ghost the config namespace.
        if (n < 1 || n > WorkspaceSlotCount) {
            qFatal("workspaceMoveSlotKey: n out of range: %d", n);
        }
        return workspaceMoveSlotKeyPattern().arg(n);
    }
    P_CONFIG_KEY(workspaceFocusSlotKeyPattern, "WorkspaceFocusSlot%1")
    static QString workspaceFocusSlotKey(int n)
    {
        if (n < 1 || n > WorkspaceSlotCount) {
            qFatal("workspaceFocusSlotKey: n out of range: %d", n);
        }
        return workspaceFocusSlotKeyPattern().arg(n);
    }
    // Quick-slot targets (Workspaces.Slots): the NAMED WORKSPACE each move
    // slot sends the active window to (quick-layout model: chord fixed per
    // slot and KCM-rebindable, target assigned in the settings app).
    P_CONFIG_KEY(workspaceSlotTargetKeyPattern, "Target%1")
    static QString workspaceSlotTargetKey(int n)
    {
        if (n < 1 || n > WorkspaceSlotCount) {
            qFatal("workspaceSlotTargetKey: n out of range: %d", n);
        }
        return workspaceSlotTargetKeyPattern().arg(n);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — workspace verb shortcuts (Shortcuts.Global leaves)
    // ═══════════════════════════════════════════════════════════════════════════

    P_CONFIG_KEY(workspaceFocusUpKey, "WorkspaceFocusUp")
    P_CONFIG_KEY(workspaceFocusDownKey, "WorkspaceFocusDown")
    P_CONFIG_KEY(workspaceMoveWindowUpKey, "WorkspaceMoveWindowUp")
    P_CONFIG_KEY(workspaceMoveWindowDownKey, "WorkspaceMoveWindowDown")
    P_CONFIG_KEY(workspaceMoveColumnUpKey, "WorkspaceMoveColumnUp")
    P_CONFIG_KEY(workspaceMoveColumnDownKey, "WorkspaceMoveColumnDown")
    P_CONFIG_KEY(workspaceReorderUpKey, "WorkspaceReorderUp")
    P_CONFIG_KEY(workspaceReorderDownKey, "WorkspaceReorderDown")
    P_CONFIG_KEY(workspaceMoveToMonitorLeftKey, "WorkspaceMoveToMonitorLeft")
    P_CONFIG_KEY(workspaceMoveToMonitorRightKey, "WorkspaceMoveToMonitorRight")
    P_CONFIG_KEY(overviewToggleKey, "OverviewToggle")

    // ═══════════════════════════════════════════════════════════════════════════
    // Config Keys — Workspaces.Overview
    // ═══════════════════════════════════════════════════════════════════════════

    // Fully open zoom factor (niri's overview.zoom).
    P_CONFIG_KEY(overviewZoomKey, "Zoom")
    // Concrete #RRGGBB / #AARRGGBB behind the zoomed-out workspaces.
    P_CONFIG_KEY(overviewBackdropColorKey, "BackdropColor")
    // Four-finger touchpad / three-finger touchscreen swipe opens it.
    P_CONFIG_KEY(overviewGestureEnabledKey, "GestureEnabled")
    // The wheel over a workspace column switches that screen's workspace.
    P_CONFIG_KEY(overviewWheelSwitchesWorkspacesKey, "WheelSwitchesWorkspaces")
    // Name labels above every workspace cell.
    P_CONFIG_KEY(overviewShowWorkspaceNamesKey, "ShowWorkspaceNames")
};

} // namespace PlasmaZones

#include "configkeymacro_undef.h"
