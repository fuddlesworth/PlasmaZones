// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.PowerButton, opens the power menu.
//
// An icon button whose `activated` signal is the seam the power menu
// popout binds to, wired to phosphor-service-session. The activation is
// relayed through BarRegistry.widgetActivated and handled by the shell
// composer, which opens the menu carrying the session actions (log out,
// suspend, reboot, shut down).

BarIconButton {
    iconName: "system-shutdown"
    label: qsTr("Power")
}
