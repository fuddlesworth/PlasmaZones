// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.ControlCenterButton, opens the control center.
//
// An icon button whose `activated` signal is the seam the control center
// popout binds to. The control center surface itself is Phase 4.4; the
// button is complete and the activation routes there once that surface
// and the bar-anchored popout wiring land (the BarCanvas socket the
// popout grows from is already in place).

import QtQuick

BarIconButton {
    iconName: "configure"
    label: "Control center"
}
