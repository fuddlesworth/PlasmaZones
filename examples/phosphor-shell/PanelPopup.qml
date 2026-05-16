// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

// Shared panel-popup chrome — one xdg_popup with a translucent animated
// gradient backdrop and a hairline border. Callers supply geometry via
// the PopupWindow properties and the body via `content`; this collapses
// the per-popup boilerplate that used to be copied three times in
// PanelPopupHost.qml.
PopupWindow {
    id: popup

    // Inset applied to the content body inside the border.
    property real contentMargins: 14
    // Body of the popup. Assigned by callers; lands inside the margin
    // holder so a child `anchors.fill: parent` resolves to the inset
    // client area rather than the full popup surface.
    property alias content: contentHolder.data

    popupEdge: PopupWindow.Below
    gap: 8
    popupVisible: false

    // Translucent animated gradient — no wallpaper sampling; the
    // compositor blends it over whatever sits behind the popup surface.
    ShaderBackground {
        anchors.fill: parent
        playing: popup.popupVisible
        shaderSource: Qt.resolvedUrl("shaders/gradient.frag")
        useWallpaper: false
        shaderParams: {
            "customParams1_x": 1.2,
            "customParams1_y": 0,
            "customParams1_z": 0.9,
            "customParams1_w": 0,
            "customParams2_x": 14,
            "customParams2_y": 24
        }
        customColor1: "#cba6f7"
        customColor2: "#89dceb"
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: 14
        border.color: "#80a6adc8"
        border.width: 1
    }

    Item {
        id: contentHolder

        anchors.fill: parent
        anchors.margins: popup.contentMargins
    }

}
