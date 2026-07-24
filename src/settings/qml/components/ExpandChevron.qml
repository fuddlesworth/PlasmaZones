// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief The rotating expand-state indicator shared by every
 * ExpandableRowDelegate consumer — extracted verbatim from RuleRow.
 *
 * Rotates 90° clockwise when `expanded` so the same icon serves as a visual
 * separator in the header AND as a chevron pointing at the expanded body. A
 * passive indicator (no input handler): the whole-row click on the hosting
 * ExpandableRowDelegate owns the toggle, which keeps this from competing with
 * the row click for the same press event.
 */
Kirigami.Icon {
    /// Bind to the hosting row's `expanded`.
    property bool expanded: false

    source: "arrow-right"
    Layout.preferredWidth: Kirigami.Units.iconSizes.small
    Layout.preferredHeight: Kirigami.Units.iconSizes.small
    Layout.alignment: Qt.AlignVCenter
    opacity: 0.6
    rotation: expanded ? 90 : 0

    Behavior on rotation {
        PhosphorMotionAnimation {
            profile: "widget.hover"
            durationOverride: Kirigami.Units.shortDuration
        }
    }
}
