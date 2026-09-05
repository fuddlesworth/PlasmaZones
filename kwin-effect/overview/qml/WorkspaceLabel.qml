// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The name pill above a workspace cell: the declared name for a named
// workspace, "Workspace N" otherwise. Becomes the drag source for whole
// workspace moves and the rename affordance once strip management lands.

import QtQuick
import org.kde.kirigami as Kirigami

Rectangle {
    id: label

    property alias text: caption.text
    property bool current: false

    implicitWidth: caption.implicitWidth + Kirigami.Units.largeSpacing * 2
    implicitHeight: caption.implicitHeight + Kirigami.Units.smallSpacing * 2
    radius: height / 2
    color: label.current ? Kirigami.Theme.highlightColor : Kirigami.Theme.backgroundColor
    opacity: 0.9

    Text {
        id: caption
        anchors.centerIn: parent
        color: label.current ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
        font: Kirigami.Theme.smallFont
        elide: Text.ElideRight
    }
}
