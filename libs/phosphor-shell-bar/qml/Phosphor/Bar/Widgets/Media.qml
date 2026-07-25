// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Media, now-playing from MPRIS.
//
// Self-contained: owns an MprisHost and follows the first player (MPRIS
// exposes no "active player", so player 0 is used). Shows a play/pause
// glyph and the artist + title; clicking toggles playback. Collapses to
// zero width when no player is present or it has no track metadata.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Service.Mpris

BarWidget {
    id: root

    readonly property int maxTitleWidth: 200

    MprisHost {
        id: host
    }

    // Re-evaluated when playerCount changes; the metadata properties below
    // carry their own NOTIFY so the readout tracks the current track.
    // Typed so QML nulls it automatically if the player is destroyed.
    readonly property MprisPlayer player: host.playerCount > 0 ? host.playerAt(0) : null
    readonly property string trackTitle: root.player ? root.player.trackTitle : ""
    readonly property string trackArtist: root.player ? root.player.trackArtist : ""
    readonly property bool isPlaying: root.player ? root.player.isPlaying : false

    // Artist and title joined the same way the visible label joins them, so
    // a track with no artist does not announce a doubled space.
    // Join only when both halves are present: a player publishing an artist
    // with no title (reachable, since `available` accepts either alone) would
    // otherwise render a dangling separator.
    readonly property string _label: {
        if (root.trackArtist.length > 0 && root.trackTitle.length > 0)
            return root.trackArtist + " · " + root.trackTitle;
        return root.trackTitle.length > 0 ? root.trackTitle : root.trackArtist;
    }

    // MPRIS has three states, so a stopped player should not announce as
    // merely paused.
    readonly property string _stateWord: {
        if (root.isPlaying)
            return "Playing";
        if (root.player && root.player.playbackState === MprisPlayer.Stopped)
            return "Stopped";
        return "Paused";
    }

    function _toggle() {
        if (root.player)
            root.player.togglePlaying();
    }

    available: root.player !== null && (root.trackTitle.length > 0 || root.trackArtist.length > 0)
    contentWidth: row.implicitWidth
    contentHeight: row.implicitHeight

    Accessible.role: Accessible.Indicator
    Accessible.name: root._label.length > 0 ? root._stateWord + " " + root._label : root._stateWord

    // A player that reports no control capability still shows its track, but
    // dimmed, so the inert click is legible rather than silently dead.
    // canControl defaults false and is only set when the player publishes
    // the key, so this also covers a player that omits it.
    readonly property bool _controllable: root.player !== null && root.player.canControl

    Row {
        id: row

        opacity: root._controllable ? 1 : StateLayer.disabled_content
        spacing: Tokens.spacing_xs

        Kirigami.Icon {
            width: 16
            height: 16
            source: root.isPlaying ? "media-playback-pause" : "media-playback-start"
            isMask: true
            color: Theme.on_surface
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            // Folded into the root's Accessible.name already; QQuickText
            // exposes itself as its own StaticText node, so without this
            // assistive tech reads the composed name and then re-reads
            // this fragment.
            Accessible.ignored: true
            text: root._label
            color: Theme.on_surface
            font.pixelSize: Tokens.font_size_label_l
            font.family: Tokens.font_family
            elide: Text.ElideRight
            // Setting width below the natural implicitWidth triggers the
            // elide; implicitWidth is intrinsic so there is no binding loop.
            width: Math.min(implicitWidth, root.maxTitleWidth)
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        // A player that cannot be controlled should not offer a
        // pointing-hand cursor and a no-op click.
        enabled: root._controllable

        // The track readout is on the root Indicator; this area is the
        // actionable control, so it announces AND performs the toggle.
        // Pointer + assistive tech only, no keyboard leg: this widget is an
        // indicator that happens to be clickable, and the bar's panel takes
        // no keyboard focus, so there is nothing to Tab from. BarIconButton
        // carries the full quad because it is the shared button atom and is
        // meant to work wherever a focused surface hosts it.
        Accessible.role: Accessible.Button
        Accessible.name: root.isPlaying ? "Pause" : "Play"
        Accessible.onPressAction: root._toggle()

        onClicked: root._toggle()
    }
}
