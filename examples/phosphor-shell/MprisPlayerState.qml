// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Service.Mpris 1.0
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
    // When false, `progress` evaluates to 0 unconditionally (the
    // early-return short-circuits before reading `player.position`,
    // so the QML binding tracker drops it as a dependency and the
    // expression stays dormant until sampling flips back to true).
    // The upstream C++ position timer keeps ticking; this only mutes
    // the QML side. Set from the consuming view's visibility to avoid
    // waking the JS engine for an off-screen widget.
    property bool sampling: true
    readonly property bool hasPlayer: player !== null
    readonly property bool isPlaying: hasPlayer && player.isPlaying
    readonly property real progress: {
        if (!sampling || !hasPlayer || player.length <= 0)
            return 0;

        return Math.min(1, Math.max(0, player.position / player.length));
    }
    // Album-art URL. A plain binding is already flicker-free: it tracks
    // both `player` and `player.trackArtUrl` (NOTIFY metadataChanged),
    // and QML suppresses the change signal when the recomputed string is
    // identical: so an Image bound to this never reloads on an
    // unrelated metadataChanged.
    readonly property string stableArtUrl: (player && player.trackArtUrl) ? player.trackArtUrl : ""

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
}
