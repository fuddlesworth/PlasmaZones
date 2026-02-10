// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * Category badge for layout type.
 * Renders a label based on the category property:
 *   0 = Manual, 1 = Auto, 2 = Dynamic
 */
Rectangle {
    id: root

    property int category: 0  // LayoutCategory: 0=Manual, 1=Auto, 2=Dynamic
    property bool autoAssign: false

    readonly property bool isDynamic: category === 2
    readonly property bool isAuto: category === 1 || autoAssign
    readonly property bool isActive: isDynamic || isAuto

    readonly property real heightScale: 0.9
    readonly property real manualBackgroundOpacity: 0.15
    readonly property real manualTextOpacity: 0.6
    readonly property real fontScale: 0.75

    implicitWidth: categoryLabel.implicitWidth + Kirigami.Units.smallSpacing * 1.5
    implicitHeight: Kirigami.Units.gridUnit * heightScale
    radius: Kirigami.Units.smallSpacing / 2

    color: root.isActive
        ? Qt.rgba(Kirigami.Theme.activeTextColor.r, Kirigami.Theme.activeTextColor.g, Kirigami.Theme.activeTextColor.b, manualBackgroundOpacity)
        : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, manualBackgroundOpacity)

    Label {
        id: categoryLabel

        anchors.centerIn: parent
        text: root.isDynamic ? i18nc("@label:badge", "Dynamic")
            : root.isAuto ? i18nc("@label:badge", "Auto")
            : i18nc("@label:badge", "Manual")
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize * root.fontScale
        font.weight: Font.Medium
        color: root.isActive ? Kirigami.Theme.activeTextColor : Kirigami.Theme.textColor
        opacity: root.isActive ? 0.8 : root.manualTextOpacity
    }
}
