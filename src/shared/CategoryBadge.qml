// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * Category badge component for distinguishing Manual vs Autotile layouts
 * Used in zone selector to show layout type with consistent styling
 */
Rectangle {
    id: root

    // Category: 0=Manual, 1=Autotile (matches LayoutCategory enum in C++)
    property int category: 0
    property bool isAutotile: category === 1

    // Sizing
    implicitWidth: categoryLabel.implicitWidth + Kirigami.Units.smallSpacing * 1.5
    implicitHeight: Kirigami.Units.gridUnit * 0.9
    radius: Kirigami.Units.smallSpacing / 2

    // Complementary styling - Auto uses Manual's text opacity as background, and vice versa
    color: root.isAutotile
        ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.6)
        : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

    Label {
        id: categoryLabel

        anchors.centerIn: parent
        text: root.isAutotile ? i18nc("@label:badge", "Auto") : i18nc("@label:badge", "Manual")
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize * 0.75
        font.weight: Font.Medium
        color: root.isAutotile ? Kirigami.Theme.backgroundColor : Kirigami.Theme.textColor
        opacity: root.isAutotile ? 1.0 : 0.6
    }
}
