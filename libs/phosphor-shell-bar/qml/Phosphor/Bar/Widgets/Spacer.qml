// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Spacer, a fixed-width gap between widgets in one chip.
//
// Belongs INSIDE a group, to push apart widgets that share a chip: the
// slot paints one pill per group, so a group containing only spacers
// renders an empty pill rather than a gap. The width is one large spacing
// token; place several in the same group for a wider gap. Carries no data
// and is never interactive.

import QtQuick
import Phosphor.Theme

Item {
    implicitWidth: Tokens.spacing_xl
    implicitHeight: 1

    Accessible.ignored: true
}
