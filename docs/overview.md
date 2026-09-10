<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Workspace overview

Feature notes for the v3.5 workspace overview. The design research is in
`docs/overview/`; this file records the user-visible policy decisions a
support answer needs.

## What it is

The overview is a zoomed-out view of every monitor's workspaces, in the shape
of niri's overview rather than Plasma's. Each monitor shows its own column of
workspaces, stacked vertically, with the current one centred and the others
above and below it. Every workspace is drawn as the screen scaled down by the
zoom setting, and the windows inside keep the exact geometry their placement
mode gave them, so a scrolling strip's columns, a tiling algorithm's layout
and a snapped layout's zones all read the same way they do live.

It opens with Meta+W by default (the Overview action in Settings →
Shortcuts), a four-finger swipe up on a touchpad, or three fingers on a touch
screen. Escape, the shortcut again, or clicking a workspace closes it. When
dynamic workspaces are on, this overview replaces KWin's stock Overview for
the toggle: the stock effect shows KWin's whole desktop pool as one flat list
and knows nothing about per-monitor ownership.

## Who draws what

The daemon is the layout authority. It builds the model (which workspace holds
which window, and where) from the three placement engines and streams it to
the KWin effect over the `org.plasmazones.Overview` interface only while the
overview is open. The effect draws it and sends every action back as a verb;
nothing in the effect moves a window or a workspace on its own. Verbs are
accepted only from KWin's own bus connection, so a script or another process
cannot drive the overview through the daemon.

## What you can do in it

- Click a workspace to switch that monitor to it and close the overview.
- Scroll the wheel over a monitor's column to move through that monitor's
  workspaces (Input → Mouse wheel switches workspaces).
- Drag a window to another workspace, on the same monitor or another one. The
  drop point decides where it lands: in a scrolling strip it becomes a new
  column at that position, or joins a column when dropped onto one; in tiling
  it takes the slot nearest the drop; in snapping it takes the zone under the
  drop, or floats there when no zone is under it.
- Drop a window in the gap between two workspaces to create a new workspace
  there. Dropping below the last workspace uses the trailing empty one
  instead of creating a second empty.
- Drag a workspace label to reorder it within its monitor, or onto another
  monitor's column to move the whole workspace there. Its windows travel with
  it and keep their layout.
- Click a workspace label to rename it. Escape cancels the edit. The pin
  button beside the name turns the workspace into a named workspace under
  its current name, which persists while empty and stays on its monitor,
  and unpinning removes that declaration again.
- Pan a scrolling strip by dragging it with the right mouse button, or with
  a horizontal wheel (Shift and the wheel does the same). The pan is
  stored, so the strip stays where you left it after the overview closes.

## Settings

Settings → Workspaces → Overview holds five settings, all applied on Save
without a restart:

- **Zoom** (10% to 75%, default 50%): how far the view zooms out.
- **Backdrop**: the colour behind the workspaces. A concrete colour by
  design, since the overview replaces the whole screen and a light colour
  scheme would otherwise put a light backdrop behind dark workspaces.
- **Workspace names**: show each workspace's name or number above it.
- **Swipe gesture**: the touchpad and touch screen swipe. The shortcut always
  works.
- **Mouse wheel switches workspaces**.

The open and close motion follows the desktop switch animation profile, so
the duration and curve set for desktop switching apply here too.

## Interactions with other features

- The overview is a fullscreen KWin effect. While it is open the main
  PlasmaZones effect suspends zone overlays, drop indicators and tab
  indicators, and any other fullscreen effect keeps the overview from opening
  until it ends.
- The daemon's own surfaces (OSD, picker, strip chrome) are never shown as
  windows in the overview, and neither are docks, panels, notifications or
  tooltips.
- A workspace created by a gap drop, or one emptied by a drag out of it,
  follows the ordinary dynamic-workspace rules: an emptied middle workspace
  is destroyed after the usual grace period, so the source of a drag can
  disappear from the column once the window has gone.
- Renames and pins made in the overview are the same named-workspace entries
  the Named Workspaces page edits, and show up there.
