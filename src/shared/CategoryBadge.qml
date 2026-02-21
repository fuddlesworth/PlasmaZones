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

    property int category: 0  // 0=Manual, 1=Autotile (matches LayoutCategory in C++)
    property bool autoAssign: false

    // Convenience: true when this entry is a dynamic tiling algorithm
    readonly property bool isDynamic: category === 1

    readonly property real heightScale: 0.9
    readonly property real backgroundOpacity: 0.15
    readonly property real textOpacity: 0.6
    readonly property real fontScale: 0.75

    implicitWidth: categoryLabel.implicitWidth + Kirigami.Units.smallSpacing * 1.5
    implicitHeight: Kirigami.Units.gridUnit * heightScale
    radius: Kirigami.Units.smallSpacing / 2

    color: {
        if (root.isDynamic)
            return Qt.rgba(Kirigami.Theme.neutralTextColor.r, Kirigami.Theme.neutralTextColor.g, Kirigami.Theme.neutralTextColor.b, backgroundOpacity);
        if (root.autoAssign)
            return Qt.rgba(Kirigami.Theme.activeTextColor.r, Kirigami.Theme.activeTextColor.g, Kirigami.Theme.activeTextColor.b, backgroundOpacity);
        return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, backgroundOpacity);
    }

    Label {
        id: categoryLabel

        anchors.centerIn: parent
        text: {
            if (root.isDynamic)
                return i18nc("@label:badge", "Dynamic");
            return root.autoAssign ? i18nc("@label:badge", "Auto") : i18nc("@label:badge", "Manual");
        }
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize * root.fontScale
        font.weight: Font.Medium
        color: {
            if (root.isDynamic)
                return Kirigami.Theme.neutralTextColor;
            if (root.autoAssign)
                return Kirigami.Theme.activeTextColor;
            return Kirigami.Theme.textColor;
        }
        opacity: (root.isDynamic || root.autoAssign) ? 0.8 : root.textOpacity
    }
}
