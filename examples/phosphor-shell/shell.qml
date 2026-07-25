// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Bar
import QtQuick

// Top-level composer for the dogfood shell. Phase 4.1 replaces the old
// single TopPanel + pushed-in data sources with the production per-screen
// bar: BarHost mounts one connected-corner bar per monitor and each bar
// widget owns its own data source (Clock its SystemClock, Battery its
// UPowerHost, Tray its StatusNotifierHost, ...), so this file no longer
// wires clock/CPU/memory/battery into a panel.
//
// BarHost reads two context properties the host process provides:
//   - PhosphorShell  (the screen model, set by ShellEngine) for PerScreen
//   - BarRegistry    (the IBarWidgetFactory owner, set by src/shell/main.cpp)
//
// The legacy panel/popup/settings demo components (TopPanel, PanelPopupHost,
// SettingsWindow, ...) still ship in this module but are no longer composed
// here; their replacements are the Phase 4 surfaces (control center,
// notification center, power menu) reached through the bar's popout seam.
Item {
    id: root

    BarHost {
        id: bar
    }
}
