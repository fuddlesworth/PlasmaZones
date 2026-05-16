// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

// Shared panel-popup chrome — one xdg_popup with a translucent animated
// gradient backdrop, a hairline border, and a soft all-around drop
// shadow. Callers supply the visible size via contentWidth/contentHeight
// and the body via `content`; this collapses the per-popup boilerplate
// that used to be copied three times in PanelPopupHost.qml.
PopupWindow {
    id: popup

    // Visible popup size — the rounded panel the user actually sees.
    property int contentWidth: 280
    property int contentHeight: 320
    // Inset of the content body inside the border.
    property real contentMargins: 14
    // Transparent ring around the visible popup that the drop shadow is
    // drawn into. The xdg_popup surface is oversized by this on every
    // side and the shader fills the ring with the shadow falloff.
    property int shadowMargin: 20
    // Body of the popup. Assigned by callers; lands inside the margin
    // holder so a child `anchors.fill: parent` resolves to the inset
    // client area rather than the full popup surface.
    property alias content: contentHolder.data

    popupEdge: PopupWindow.Below
    // The xdg_popup surface carries the shadow ring, so it is larger
    // than the visible popup on every side...
    popupWidth: contentWidth + 2 * shadowMargin
    popupHeight: contentHeight + 2 * shadowMargin
    // ...and its top edge starts shadowMargin px above the visible
    // popup. The intended 8 px visual gap below the anchor therefore
    // becomes (8 - shadowMargin) once expressed as the surface gap fed
    // to the xdg-positioner.
    gap: 8 - shadowMargin
    popupVisible: false

    // Translucent animated gradient + all-around drop shadow. The shader
    // runs in popup mode because shadowMargin > 0: it draws the gradient
    // panel inset by shadowMargin and the shadow into the surrounding
    // ring (see shaders/gradient.frag).
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
            "customParams2_y": 24,
            "customParams3_x": popup.shadowMargin,
            "customParams4_y": 0.45
        }
        customColor1: "#cba6f7"
        customColor2: "#89dceb"
    }

    // Hairline border around the VISIBLE popup, inset past the shadow ring.
    Rectangle {
        anchors.fill: parent
        anchors.margins: popup.shadowMargin
        color: "transparent"
        radius: 14
        border.color: "#80a6adc8"
        border.width: 1
    }

    Item {
        id: contentHolder

        anchors.fill: parent
        anchors.margins: popup.shadowMargin + popup.contentMargins
    }

}
