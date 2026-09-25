// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import "../../js/WizardUtils.js" as WizardUtils
import org.kde.kirigami as Kirigami

/**
 * @brief Rounded config card with subtle background used in wizard step 2.
 *
 * Provides the outer chrome; content is supplied via the default property.
 */
Rectangle {
    id: root

    default property alias content: innerColumn.data
    readonly property var _colors: WizardUtils.wizardColors(Kirigami.Theme.alternateBackgroundColor, Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast), Kirigami.Theme.highlightColor, Kirigami.Theme.hoverColor)

    Layout.fillWidth: true
    implicitHeight: innerColumn.implicitHeight + Kirigami.Units.largeSpacing * 2
    radius: Kirigami.Units.smallSpacing * 2
    color: _colors.subtleBg
    border.width: 1
    border.color: _colors.subtleBorder

    ColumnLayout {
        id: innerColumn

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing
    }
}
