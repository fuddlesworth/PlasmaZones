// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * Category badge for layout type (Manual/Auto zone-based layouts).
 * The label switches between "Auto" and "Manual" based on the autoAssign flag.
 * The category property is retained for future extension (e.g. different badge
 * styles per category) but currently does not affect rendering.
 */
Rectangle {
    id: root

    property int category: 0  // 0=Manual (matches LayoutCategory in C++); reserved for future styling
    property bool autoAssign: false

    readonly property real heightScale: 0.9
    readonly property real manualBackgroundOpacity: 0.15
    readonly property real manualTextOpacity: 0.6
    readonly property real fontScale: 0.75

    implicitWidth: categoryLabel.implicitWidth + Kirigami.Units.smallSpacing * 1.5
    implicitHeight: Kirigami.Units.gridUnit * heightScale
    radius: Kirigami.Units.smallSpacing / 2

    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, manualBackgroundOpacity)

    Label {
        id: categoryLabel

        anchors.centerIn: parent
        text: root.autoAssign ? i18nc("@label:badge", "Auto") : i18nc("@label:badge", "Manual")
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize * root.fontScale
        font.weight: Font.Medium
        color: Kirigami.Theme.textColor
        opacity: root.manualTextOpacity
    }
}
