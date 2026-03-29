// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import "WizardUtils.js" as WizardUtils
import org.kde.kirigami as Kirigami

/**
 * @brief Pill-shaped badge anchored to bottom-center of parent, showing a template name.
 *
 * Used in wizard step 2 preview containers.
 */
Rectangle {
    id: root

    required property string text
    readonly property var _colors: WizardUtils.wizardColors(Kirigami.Theme.textColor, Kirigami.Theme.highlightColor)

    anchors.bottom: parent.bottom
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottomMargin: Kirigami.Units.smallSpacing
    width: badgeLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
    height: badgeLabel.implicitHeight + Kirigami.Units.smallSpacing * 2
    radius: height / 2
    color: _colors.badgeBg
    border.width: Math.round(Kirigami.Units.devicePixelRatio)
    border.color: _colors.badgeBorder

    Label {
        id: badgeLabel

        anchors.centerIn: parent
        text: root.text
        font.weight: Font.DemiBold
    }

}
