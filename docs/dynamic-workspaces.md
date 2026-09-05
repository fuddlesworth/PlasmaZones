<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Dynamic per-monitor workspaces

Feature notes for the v3.5 dynamic-workspaces layer. The full design is in
`docs/dynamic-workspaces-plan.md`; this file records the user-visible policy
decisions a support answer needs.

## Model

Each monitor owns an ordered list of virtual desktops ("its workspaces") layered
over KWin's single shared desktop pool. The daemon's ownership map
(`libs/phosphor-workspaces`) is the authority; KWin remains the authority on
which desktops exist. Occupying a monitor's last empty workspace appends a new
one; emptying a middle workspace destroys it. Named workspaces (Settings →
Workspaces) persist while empty and can be pinned to an output. On unplug a
monitor's workspaces foster onto a surviving screen and migrate home on replug.
The map persists across restarts in `~/.local/state/plasmazones/plasmazonesd/workspaces.json`.

## Requirements and consent

The feature requires KWin's per-output virtual desktops
(`PerOutputVirtualDesktops` in kwinrc, Plasma 6.7). Enabling it with that off
asks for consent, then writes the key and reconfigures KWin (applies
immediately). PlasmaZones never writes the key silently and never reverts it
when the feature is disabled.

## The Pager and stock desktop shortcuts are unsupported-by-policy

Plasma's Pager, the stock Overview, and the Task Manager desktop filters render
KWin's whole desktop pool and know nothing about per-monitor ownership. They
will show every monitor's workspaces as one flat list. Switching to another
monitor's workspace through them triggers the owner-wins snap-back (with an OSD
hint, toggleable). Hide the Pager widget when using dynamic workspaces.

KWin's stock "Switch One Desktop" shortcuts walk the same global pool, so while
the feature is on they are taken over by default (backed up and restored on
disable; toggleable in Settings → Workspaces). The PlasmaZones workspace
shortcuts (`Meta+Ctrl+Up/Down` and friends) walk only the current monitor's own
workspace list.
