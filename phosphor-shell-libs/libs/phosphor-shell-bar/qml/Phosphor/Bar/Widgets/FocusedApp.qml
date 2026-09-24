// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme
import Phosphor.Shell

BarWidget {
    id: root
    property real railT: 0.15
    readonly property var placementMap: Screen.name ? PlacementMap.forScreen(Screen.name) : null
    readonly property var activeWindow: Toplevels.activeToplevel || (placementMap ? placementMap.windows.find(w => w.focused) : null)
    readonly property string appId: activeWindow ? activeWindow.appId.split(".").pop() : ""
    readonly property string appName: appId ? appId.charAt(0).toUpperCase() + appId.slice(1) : qsTr("Desktop")
    contentWidth: label.width
    contentHeight: 20
    Text {
        id: label
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(175, implicitWidth)
        text: root.appName + (root.activeWindow ? "   /   " + root.activeWindow.title : "")
        color: Appearance.muted
        elide: Text.ElideRight
        font.family: Tokens.font_family_ui
        font.pixelSize: Math.round((11) * Appearance.textScale)
    }
}
