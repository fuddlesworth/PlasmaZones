// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * A single key cap ("Ctrl", "Shift", "F9") rendered as a bordered chip.
 * Used by CheatsheetContent to display key sequences one token per chip.
 */
Rectangle {
    id: root

    /// The key token to display.
    property alias text: keyLabel.text
    /// User font settings, forwarded by the host so the caps track the
    /// row labels (same family, same scale) instead of diverging from
    /// them. The 0.9 factor keeps caps slightly compact relative to the
    /// labels at every scale.
    property string fontFamily: ""
    property real fontSizeScale: 1
    /// Draw as a leading modifier rather than the key the row is really
    /// about: no fill, dimmer text. A cheatsheet repeats the same two or
    /// three modifiers on nearly every row, and at uniform weight the eye
    /// has to parse the whole run to find the one token that differs.
    /// Recessing the modifiers leaves the terminal cap as the only thing
    /// at full contrast, so a row can be read by its last chip alone.
    property bool dimmed: false

    /// True when this cap's text answers the sheet's current query. The chip
    /// picks up the accent the way a matched run of a label picks up bold,
    /// which is the only way a query typed against key text can show where it
    /// landed.
    property bool highlighted: false

    implicitWidth: Math.max(keyLabel.implicitWidth + Kirigami.Units.smallSpacing * 2, implicitHeight)
    implicitHeight: keyLabel.implicitHeight + Kirigami.Units.smallSpacing
    radius: Kirigami.Units.smallSpacing
    color: root.highlighted ? Qt.alpha(Kirigami.Theme.highlightColor, 0.22) : Qt.alpha(Kirigami.Theme.textColor, root.dimmed ? 0 : 0.08)
    border.width: 1
    border.color: root.highlighted ? Qt.alpha(Kirigami.Theme.highlightColor, 0.7) : Qt.alpha(Kirigami.Theme.textColor, root.dimmed ? 0.16 : 0.25)

    Label {
        id: keyLabel

        anchors.centerIn: parent
        font.family: root.fontFamily.length > 0 ? root.fontFamily : Kirigami.Theme.defaultFont.family
        font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * 0.9 * root.fontSizeScale)
        color: root.dimmed ? Qt.alpha(Kirigami.Theme.textColor, 0.6) : Kirigami.Theme.textColor
        // The hosting shortcut row announces a composed "action, keys" name;
        // the per-token caps must not be announced a second time.
        Accessible.ignored: true
    }
}
