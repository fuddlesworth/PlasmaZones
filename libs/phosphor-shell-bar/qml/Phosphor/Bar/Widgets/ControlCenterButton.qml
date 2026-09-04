// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.ControlCenterButton, opens the control center.
//
// An icon button whose `activated` signal is the seam the control center
// popout binds to. The activation is relayed through
// BarRegistry.widgetActivated and handled by the shell composer, which
// opens the control center into the BarCanvas socket this bar already
// paints.

BarIconButton {
    iconName: "configure"
    label: qsTr("Control center")
}
