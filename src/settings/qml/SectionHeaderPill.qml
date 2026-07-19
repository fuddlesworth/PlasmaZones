// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

import "FontUtils.js" as FontUtils

/**
 * @brief Small uppercase capsule heading for a section inside an expanded row.
 *
 * One capsule style shared by every expansion that splits its body into
 * labelled halves — the rule row's WHEN / THEN preview and the profile row's
 * SETTINGS / RULES diff — so the two read as one design. Fill and border come
 * from the highlight colour family (0.4 / 0.9 alpha), matching the ALL/ANY/NONE
 * group pills in MatchExpressionView.
 */
Rectangle {
    property alias text: pillLabel.text

    implicitWidth: pillLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
    implicitHeight: pillLabel.implicitHeight + Kirigami.Units.smallSpacing * 2
    radius: implicitHeight / 2
    color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4)
    border.width: 1
    border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.9)

    Label {
        id: pillLabel

        anchors.centerIn: parent
        // One binding: a font.<sub> sibling next to a whole-group `font:` is an
        // illegal duplicate binding that fails the whole document. FontUtils
        // passes only the size dimension the theme font actually carries.
        font: FontUtils.withProps(Kirigami.Theme.smallFont, {
            bold: true,
            capitalization: Font.AllUppercase
        })
        color: Kirigami.Theme.textColor
    }
}
