// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.PowerButton, opens the power menu.
//
// An icon button whose `activated` signal is the seam the power menu
// popout binds to (Phase 4.6, wired to phosphor-service-session). The
// button is complete; the session actions (log out, suspend, reboot,
// shut down) live behind that popout.

BarIconButton {
    iconName: "system-shutdown"
    label: qsTr("Power")
}
