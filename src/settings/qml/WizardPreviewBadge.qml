// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Bottom name bar for the wizard step-2 preview containers.
 *
 * Mirrors the layout card's name label (owned by the shared LayoutCard
 * component) so every preview in the app names its content the same way:
 * a full-width, centered label over a translucent strip at the bottom of
 * the frame.
 */
Label {
    anchors.bottom: parent.bottom
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.margins: Kirigami.Units.smallSpacing
    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
    font.weight: Font.DemiBold
    elide: Text.ElideRight
    horizontalAlignment: Text.AlignHCenter

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.9)
        radius: Kirigami.Units.smallSpacing * 0.5
    }
}
