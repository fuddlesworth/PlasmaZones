// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Services 1.0
import Phosphor.Shell 1.0
import QtQuick
import QtQuick.Window

// Coordinator + three independent PopupWindows (one per panel kind).
//
// Pattern lifted from noctalia-shell — each panel is its own permanently-
// instantiated entity with its own content, and a coordinator
// (PanelService.willOpenPanel) closes the previously-open panel before
// opening the next one. There is no shared content host whose child
// Loader gets reassigned at runtime — that pattern doesn't work cleanly
// with Qt's PopupWindow + xdg_popup grab semantics, because the
// content-swap inside an already-mapped popup leaves either bindings
// disconnected (post-reparent) or animations interrupted.
//
// Switching popups: when the user clicks a different panel toggle while
// one popup is open, we set `pendingKind = "<new>"` and close the
// current popup synchronously. The current popup's `popupVisibleChanged`
// fires when the compositor confirms unmap (event-driven, not timed),
// at which point we open the queued popup. This is correct without any
// timer because each popup is a separate xdg_popup — no grab transfer
// needed, just a clean unmap-then-map sequence.
Item {
    id: host

    required property var shellState
    required property var topPanel

    // "calendar" | "media" | "menu" | "none". External read-only state.
    readonly property string currentKind: calendarPopup.popupVisible ? "calendar" : mediaPopup.popupVisible ? "media" : menuPopup.popupVisible ? "menu" : "none"

    // Internal queue: kind to open after the current popup unmaps.
    property string _pendingKind: ""

    function toggle(kind) {
        // Toggle off if the requested kind is already open.
        if (currentKind === kind) {
            _closeAll();
            return;
        }
        // If something else is open, queue and close. The unmap will
        // trigger the queued open via _maybeOpenPending.
        if (currentKind !== "none") {
            _pendingKind = kind;
            _closeAll();
            return;
        }
        // Nothing open — open immediately.
        _open(kind);
    }

    function _closeAll() {
        calendarPopup.popupVisible = false;
        mediaPopup.popupVisible = false;
        menuPopup.popupVisible = false;
    }

    function _open(kind) {
        if (kind === "calendar")
            calendarPopup.popupVisible = true;
        else if (kind === "media")
            mediaPopup.popupVisible = true;
        else if (kind === "menu")
            menuPopup.popupVisible = true;
    }

    function _maybeOpenPending() {
        if (_pendingKind === "")
            return;
        if (currentKind !== "none")
            return;
        const next = _pendingKind;
        _pendingKind = "";
        _open(next);
    }

    // Mirror to shellState booleans for any external binding.
    onCurrentKindChanged: {
        shellState.calendarOpen = currentKind === "calendar";
        shellState.mediaOpen = currentKind === "media";
        shellState.menuOpen = currentKind === "menu";
        // When the active popup unmaps and currentKind drops to "none",
        // open whatever was queued.
        if (currentKind === "none")
            _maybeOpenPending();
    }

    PopupWindow {
        id: calendarPopup
        anchor: host.topPanel.calendarAnchor
        popupEdge: PopupWindow.Below
        popupWidth: 280
        popupHeight: 320
        gap: 8
        popupVisible: false

        readonly property real popupToScreenH: popupHeight / Math.max(Screen.height, 1)
        readonly property real popupScreenY: (host.topPanel.panelSurfaceHeight + gap) / Math.max(Screen.height, 1)

        ShaderBackground {
            anchors.fill: parent
            playing: calendarPopup.popupVisible
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
                "customParams3_x": calendarPopup.popupToScreenH,
                "customParams3_y": 8,
                "customParams4_x": 0,
                "customParams4_y": 0,
                "customParams6_y": calendarPopup.popupScreenY
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
        CalendarContent {
            anchors.fill: parent
            anchors.margins: 14
            active: calendarPopup.popupVisible
            shellState: host.shellState
        }
    }

    PopupWindow {
        id: mediaPopup
        anchor: host.topPanel.mediaAnchor
        popupEdge: PopupWindow.Below
        popupWidth: 300
        popupHeight: 360
        gap: 8
        popupVisible: false

        readonly property real popupToScreenH: popupHeight / Math.max(Screen.height, 1)
        readonly property real popupScreenY: (host.topPanel.panelSurfaceHeight + gap) / Math.max(Screen.height, 1)

        ShaderBackground {
            anchors.fill: parent
            playing: mediaPopup.popupVisible
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
                "customParams3_x": mediaPopup.popupToScreenH,
                "customParams3_y": 8,
                "customParams4_x": 0,
                "customParams4_y": 0,
                "customParams6_y": mediaPopup.popupScreenY
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
        MprisContent {
            anchors.fill: parent
            anchors.margins: 16
            active: mediaPopup.popupVisible
            shellState: host.shellState
            currentPlayer: host.topPanel.mediaPlayer
        }
    }

    PopupWindow {
        id: menuPopup
        anchor: host.topPanel.menuAnchor
        popupEdge: PopupWindow.Below
        popupWidth: 220
        popupHeight: 240
        gap: 8
        popupVisible: false

        readonly property real popupToScreenH: popupHeight / Math.max(Screen.height, 1)
        readonly property real popupScreenY: (host.topPanel.panelSurfaceHeight + gap) / Math.max(Screen.height, 1)

        ShaderBackground {
            anchors.fill: parent
            playing: menuPopup.popupVisible
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
                "customParams3_x": menuPopup.popupToScreenH,
                "customParams3_y": 8,
                "customParams4_x": 0,
                "customParams4_y": 0,
                "customParams6_y": menuPopup.popupScreenY
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
        MenuContent {
            anchors.fill: parent
            anchors.margins: 8
            active: menuPopup.popupVisible
            shellState: host.shellState
        }
    }
}
