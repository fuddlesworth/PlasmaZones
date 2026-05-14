// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Services 1.0
import Phosphor.Shell 1.0
import QtQuick
import QtQuick.Window

// Single-surface host for the panel's three popups (calendar / media /
// menu). Owns one PopupWindow whose anchor + size + content swap when
// `currentKind` changes. There is only ever one xdg_popup mapped from
// this client at a time, so popup-to-popup transitions can't race the
// xdg_popup grab handoff that bit the previous three-PopupWindow
// implementation. PopupWindow.setAnchor (libs/phosphor-shell/src/
// popupwindow.cpp:32) calls reapplyIfVisible which destroys + recreates
// the xdg_popup in the same JS callstack — Qt batches the Wayland
// destroy+create messages, so the compositor sees an atomic switch
// rather than two simultaneous popups from the same parent.
PopupWindow {
    id: root

    required property var shellState
    required property var topPanel

    // "calendar" | "media" | "menu" | "none"
    property string currentKind: "none"

    function _anchorFor(kind) {
        if (kind === "calendar")
            return topPanel.calendarAnchor;
        if (kind === "media")
            return topPanel.mediaAnchor;
        if (kind === "menu")
            return topPanel.menuAnchor;
        return null;
    }
    function _widthFor(kind) {
        if (kind === "calendar")
            return 280;
        if (kind === "media")
            return 300;
        if (kind === "menu")
            return 220;
        return 1;
    }
    function _heightFor(kind) {
        if (kind === "calendar")
            return 320;
        if (kind === "media")
            return 360;
        if (kind === "menu")
            return 240;
        return 1;
    }

    onCurrentKindChanged: {
        // Mirror to shellState.*Open booleans for compatibility with
        // any binding that still keys on them. shellState is a
        // PersistentProperties singleton; bindings on it propagate
        // properly across the PopupWindow → QQuickWindow::contentItem
        // re-parent that happens on first show (popupwindow.cpp:213).
        // Bindings that walk through `root.currentKind` directly do
        // NOT re-evaluate after the reparent (likely because the
        // property-change notification path is severed when the child's
        // ancestry no longer includes the original root). So content
        // visibility is keyed on shellState.*Open booleans below
        // instead of root.currentKind.
        shellState.calendarOpen = currentKind === "calendar";
        shellState.mediaOpen = currentKind === "media";
        shellState.menuOpen = currentKind === "menu";

        if (currentKind === "none") {
            popupVisible = false;
            return;
        }
        popupWidth = _widthFor(currentKind);
        popupHeight = _heightFor(currentKind);
        anchor = _anchorFor(currentKind);
        // setAnchor's reapplyIfVisible already replays the positioner;
        // setting popupVisible = true here is the open path when no
        // popup was previously mapped.
        popupVisible = true;
    }

    onPopupVisibleChanged: {
        // Compositor-side dismissal (click outside, Esc) — drop back
        // to "none" so the next toggle sees a clean state.
        if (!popupVisible && currentKind !== "none")
            currentKind = "none";
    }

    popupEdge: PopupWindow.Below
    popupWidth: 280
    popupHeight: 320
    gap: 8
    popupVisible: false

    readonly property real popupToScreenH: popupHeight / Math.max(Screen.height, 1)
    readonly property real popupScreenY: (topPanel.panelSurfaceHeight + gap) / Math.max(Screen.height, 1)

    ShaderBackground {
        anchors.fill: parent
        playing: root.popupVisible
        shaderSource: Qt.resolvedUrl("shaders/gradient.frag")
        useWallpaper: true
        wallpaperTexture: PhosphorShell.wallpaper.image
        shaderParams: {
            "customParams1_x": 1.2,
            "customParams1_y": 0,
            "customParams1_z": 0.55,
            "customParams1_w": 0,
            "customParams2_x": 14,
            "customParams2_y": 24,
            "customParams3_x": root.popupToScreenH,
            "customParams3_y": 8,
            "customParams4_x": 0,
            "customParams4_y": 0,
            "customParams6_y": root.popupScreenY
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

    // Three sibling content Items, all instantiated up-front. The
    // active one is shown via z-order and the inactive ones are hidden
    // by setting `active: false` (which their internal Behaviors fade
    // to opacity 0) AND `visible: false` so they don't intercept input.
    //
    // PopupWindow reparents direct children to QQuickWindow::contentItem
    // on first show (popupwindow.cpp:160-167), and itemChange does the
    // same for late-added children (popupwindow.cpp:323-334). Both work
    // here because each content is a direct child of the PopupWindow
    // QQuickItem, not nested inside a Loader/Item wrapper.
    CalendarContent {
        anchors.fill: parent
        anchors.margins: 14
        active: root.shellState.calendarOpen
        visible: active
        shellState: root.shellState
    }

    MprisContent {
        anchors.fill: parent
        anchors.margins: 16
        active: root.shellState.mediaOpen
        visible: active
        shellState: root.shellState
        currentPlayer: root.topPanel.mediaPlayer
    }

    MenuContent {
        anchors.fill: parent
        anchors.margins: 8
        active: root.shellState.menuOpen
        visible: active
        shellState: root.shellState
    }
}
