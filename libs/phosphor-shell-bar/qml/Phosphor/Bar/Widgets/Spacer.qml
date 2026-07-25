// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Spacer, a fixed-width gap between bar widgets.
//
// A layout filler for separating groups of widgets within a slot. The
// width is one large spacing token; place several in a row for a wider
// gap. Carries no data and is never interactive.

import QtQuick
import Phosphor.Theme

Item {
    implicitWidth: Tokens.spacing_xl
    implicitHeight: 1

    Accessible.ignored: true
}
