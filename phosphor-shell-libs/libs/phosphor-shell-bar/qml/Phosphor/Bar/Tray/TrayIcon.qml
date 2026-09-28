// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme

Item {
    id: root
    property var entry: ({})
    property bool indicator: true
    property color tint: Appearance.muted
    implicitWidth: 21
    implicitHeight: 21
    Kirigami.Icon {
        anchors.fill: parent
        source: root.entry.attention && root.entry.attentionIconUrl ? root.entry.attentionIconUrl : root.entry.iconUrl || "application-x-executable"
        fallback: "application-x-executable"
        isMask: Appearance.settings.trayIcons === "symbolic"
        color: root.tint
    }
    Kirigami.Icon {
        visible: !!root.entry.overlayIconUrl
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: Math.max(9, root.width * 0.5)
        height: width
        source: root.entry.overlayIconUrl || ""
        isMask: Appearance.settings.trayIcons === "symbolic"
        color: root.tint
    }
    Rectangle {
        visible: root.indicator && !!root.entry.attention
        anchors.right: parent.right
        anchors.top: parent.top
        width: 6
        height: 6
        radius: 3
        color: Appearance.stops[3]
        border.width: 1
        border.color: Appearance.surface
    }
    Accessible.ignored: true
}
