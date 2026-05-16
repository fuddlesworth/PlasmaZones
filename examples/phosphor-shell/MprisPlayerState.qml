// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Services 1.0
import QtQuick

// Non-visual helper bundling the derived MPRIS state shared by the panel
// capsule (MprisWidget) and the popup body (MprisContent): playback
// flags, interpolated progress, and the flicker-free album-art URL.
// Both views forward to one of these so the logic lives in a single
// place.
QtObject {
    id: state

    // Player to observe; null when none is selected.
    property MprisPlayer player: null
    // When false, `progress` freezes at 0 — set from the consuming
    // view's visibility so the 1 Hz position binding doesn't wake the
    // JS engine for an off-screen widget.
    property bool sampling: true
    readonly property bool hasPlayer: player !== null
    readonly property bool isPlaying: hasPlayer && player.isPlaying
    readonly property real progress: {
        if (!sampling || !hasPlayer || player.length <= 0)
            return 0;

        return Math.min(1, Math.max(0, player.position / player.length));
    }
    // Stable art URL — only updates when the actual URL string changes,
    // preventing Image reload flicker on unrelated metadataChanged
    // signals.
    property string stableArtUrl: ""
    // metadataChanged on the active player re-evaluates the art URL.
    // Held in a property because QtObject has no default child list.
    property var _metaConn

    _metaConn: Connections {
        function onMetadataChanged() {
            state._updateArtUrl();
        }

        target: state.player
        enabled: state.player !== null
    }

    function _updateArtUrl() {
        // Gate on `player` directly, not `hasPlayer`. hasPlayer is a
        // bound property derived from player; when this runs from
        // onPlayerChanged the assignment to player has happened but
        // hasPlayer's binding may not have re-evaluated yet (Qt
        // evaluates property dependencies lazily, and the change handler
        // runs before downstream re-evaluations flush). Reading hasPlayer
        // here can still see `false` even though player is non-null —
        // the ternary then falls to "" and stableArtUrl never gets set.
        let url = (player && player.trackArtUrl) ? player.trackArtUrl : "";
        if (stableArtUrl !== url)
            stableArtUrl = url;

    }

    // Formats a duration in seconds as H:MM:SS / M:SS.
    function fmt(secs) {
        if (isNaN(secs) || secs < 0)
            return "0:00";

        let h = Math.floor(secs / 3600);
        let m = Math.floor((secs % 3600) / 60);
        let s = Math.floor(secs % 60);
        let ms = (s < 10 ? "0" : "") + s;
        if (h > 0)
            return h + ":" + (m < 10 ? "0" : "") + m + ":" + ms;

        return m + ":" + ms;
    }

    onPlayerChanged: _updateArtUrl()
}
