// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief A wrapping chip row that slides + fades open/closed.
 *
 * The root IS the Flow, so chips are declared as its direct children (no
 * content-slot needed). `open` drives a `revealFraction` (0…1) that animates
 * `Layout.preferredHeight` and opacity, clipping the chips during the reveal so
 * it reads like a drop-down. Pair with FilterDisclosureHeader:
 *
 *   FilterDisclosureHeader { id: h; hasActiveFilters: ... }
 *   CollapsibleChipFlow { open: h.expanded; <chips> }
 */
Flow {
    id: root

    /// Show (true) or hide (false) the chips.
    property bool open: false
    // Animated 0…1 reveal. A plain property (Behavior on attached Layout
    // properties is unreliable) that scales the clipped height + opacity.
    property real revealFraction: open ? 1 : 0

    Layout.fillWidth: true
    spacing: Kirigami.Units.smallSpacing
    clip: true
    // implicitHeight is the Flow's natural wrapped content height; reveal scales
    // it. preferredHeight (not implicitHeight) drives the layout so the row
    // animates without feeding back into the content measurement.
    Layout.preferredHeight: implicitHeight * revealFraction
    opacity: revealFraction
    // Drop out of the layout once fully closed (no leftover spacing gap); the
    // `|| open` keeps it laid out the instant it starts opening so the Flow
    // measures its real width before the reveal animates.
    visible: open || revealFraction > 0

    Behavior on revealFraction {
        NumberAnimation {
            duration: Kirigami.Units.shortDuration
            easing.type: Easing.InOutCubic
        }
    }
}
