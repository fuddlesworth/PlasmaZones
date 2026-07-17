// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * Category badge for layout type (Manual/Auto/Dynamic layouts).
 * - category 0 (Manual): shows "Auto" or "Manual" based on autoAssign flag
 * - category 1 (Dynamic): shows "Dynamic" for autotile algorithm entries
 */
Rectangle {
    id: root

    property int category: 0 // 0=Manual, 1=Autotile (matches LayoutCategory in C++)
    property bool autoAssign: false
    // Forces the badge into the "Auto" appearance regardless of the per-layout
    // autoAssign flag, used when the "Auto-assign for all layouts" master toggle
    // is on so the displayed state matches actual snap behavior (#370).
    property bool globalAutoAssign: false
    readonly property bool effectiveAutoAssign: autoAssign || globalAutoAssign
    // Convenience: true when this entry is a dynamic tiling algorithm
    readonly property bool isDynamic: category === 1
    readonly property real heightScale: 0.9
    readonly property real backgroundOpacity: 0.15
    // 0.7 matches the sibling badge recipe (AspectRatioBadge) and keeps the
    // "Manual" label above the 4.5:1 contrast threshold; 0.6 fell to ~3.7:1.
    readonly property real textOpacity: 0.7
    readonly property real fontScale: 0.75

    implicitWidth: categoryLabel.implicitWidth + Kirigami.Units.smallSpacing * 1.5
    implicitHeight: Kirigami.Units.gridUnit * heightScale
    radius: Kirigami.Units.smallSpacing / 2
    color: {
        if (root.isDynamic || root.effectiveAutoAssign)
            return Qt.alpha(Kirigami.Theme.highlightColor, backgroundOpacity);

        return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, backgroundOpacity);
    }

    Label {
        id: categoryLabel

        anchors.centerIn: parent
        text: {
            if (root.isDynamic)
                return i18nc("@label:badge", "Dynamic");

            return root.effectiveAutoAssign ? i18nc("@label:badge", "Auto") : i18nc("@label:badge", "Manual");
        }
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize * root.fontScale
        font.weight: Font.Medium
        // Per-category label hues were deliberately retired — all states use the plain text color.
        color: Kirigami.Theme.textColor
        opacity: (root.isDynamic || root.effectiveAutoAssign) ? 0.8 : root.textOpacity
    }
}
