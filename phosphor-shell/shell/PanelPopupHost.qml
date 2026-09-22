// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Coordinator + three independent PanelPopups (one per panel kind).
// Pattern lifted from noctalia-shell — each panel is its own permanently-
// instantiated entity with its own content, and a coordinator closes the
// previously-open panel before opening the next one. There is no shared
// content host whose child Loader gets reassigned at runtime — that
// pattern doesn't work cleanly with Qt's PopupWindow + xdg_popup grab
// semantics, because the content-swap inside an already-mapped popup
// leaves either bindings disconnected (post-reparent) or animations
// interrupted.

import QtQuick

// Switching popups: when the user clicks a different panel toggle while
// one popup is open, we set `_pendingKind`, close the current popup, and
// arm `switchTimer`. The timer (not an event) bridges the gap because
// Qt's xdg-shell client exposes no signal that fires *after* the
// compositor has actually destroyed the wl_surface — see switchTimer's
// comment for the full grab-handoff rationale.
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
        // If something else is open — OR a switch is already in flight —
        // queue and close. `currentKind` flips to "none" synchronously
        // when _closeAll() clears the popupVisible flags, but the
        // compositor has not yet acked the unmap; opening immediately in
        // that window races the xdg_popup grab handoff. switchTimer.running
        // is the true "switch in progress" signal, so gate on it too.
        if (currentKind !== "none" || switchTimer.running) {
            _pendingKind = kind;
            _closeAll();
            switchTimer.restart();
            return;
        }
        // Nothing open and no switch pending — open immediately.
        _pendingKind = "";
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
    }

    // Wayland xdg_popup has grab semantics: only one popup-with-grab
    // per parent surface at a time. When we close popup A and open B
    // synchronously, Qt's QQuickWindow::hide() returns immediately but
    // the compositor's wl_surface destroy + grab release hasn't been
    // processed yet — we're still in the same event-loop iteration.
    // Qt fires popupVisibleChanged(false) synchronously inside hide(),
    // which is before the compositor confirms unmap. If we open B at
    // that point, the compositor sees a new grab while A's grab is
    // still pending and dismisses B as non-topmost ("opens then closes
    // immediately"). Qt's xdg-shell client doesn't expose any signal
    // that fires *after* the wl_surface is actually destroyed, so we
    // bridge the round-trip with a short timer. 80ms is below human
    // perception of latency for click-to-show and is enough for any
    // common compositor (KWin/Mutter/wlroots) to round-trip the
    // unmap. Same approach noctalia/quickshell don't need because
    // they use wlr-layer-shell which has no grab semantics.
    Timer {
        id: switchTimer

        interval: 80
        repeat: false
        onTriggered: host._maybeOpenPending()
    }

    PanelPopup {
        id: calendarPopup

        anchor: host.topPanel.calendarAnchor
        contentWidth: 280
        contentHeight: 320
        contentMargins: 14

        content: CalendarContent {
            anchors.fill: parent
            active: calendarPopup.popupVisible
            shellState: host.shellState
        }
    }

    PanelPopup {
        id: mediaPopup

        anchor: host.topPanel.mediaAnchor
        contentWidth: 300
        contentHeight: 360
        contentMargins: 16

        content: MprisContent {
            anchors.fill: parent
            active: mediaPopup.popupVisible
            currentPlayer: host.topPanel.mediaPlayer
        }
    }

    PanelPopup {
        id: menuPopup

        anchor: host.topPanel.menuAnchor
        contentWidth: 220
        contentHeight: 240
        contentMargins: 8

        content: MenuContent {
            anchors.fill: parent
            active: menuPopup.popupVisible
            shellState: host.shellState
        }
    }
}
