// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Services 1.0
import Phosphor.Shell 1.0
import QtQuick

// Compact MPRIS media controls for the top panel's right zone.
// Selects the best player reactively (playing > paused > first),
// hides when no player exists. Ported from the noctalia-shell
// MediaService pattern.
Item {
    id: root

    // Reactive player selection — re-evaluates on playerCount, playback
    // state changes, and player add/remove. Uses a Connections block
    // rather than a binding expression so intermediate signal changes
    // don't cause stale reads.
    property MprisPlayer currentPlayer: null
    readonly property bool hasPlayer: currentPlayer !== null
    readonly property bool isPlaying: hasPlayer && currentPlayer.isPlaying

    visible: hasPlayer
    implicitWidth: visible ? mediaRow.implicitWidth : 0
    implicitHeight: parent ? parent.height : 26

    MprisHost {
        id: mprisHost
    }

    MprisPlayerModel {
        id: playerModel
        host: mprisHost
    }

    function selectPlayer() {
        let playing = null;
        let paused = null;
        for (let i = 0; i < mprisHost.playerCount; i++) {
            let p = mprisHost.playerAt(i);
            if (!p) continue;
            if (p.isPlaying) { playing = p; break; }
            if (p.playbackState === MprisPlayer.Paused && !paused) paused = p;
        }
        let next = playing || paused || (mprisHost.playerCount > 0 ? mprisHost.playerAt(0) : null);
        if (root.currentPlayer !== next)
            root.currentPlayer = next;
    }

    Connections {
        target: mprisHost
        function onPlayerAdded() { root.selectPlayer(); }
        function onPlayerRemoved() { root.selectPlayer(); }
        function onPlayerCountChanged() { root.selectPlayer(); }
    }

    // Re-select when any player changes playback state.
    Connections {
        target: root.currentPlayer
        enabled: root.currentPlayer !== null
        function onPlaybackStateChanged() { root.selectPlayer(); }
    }

    // Scan on startup.
    Component.onCompleted: selectPlayer()

    Row {
        id: mediaRow

        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        // Track info: artist · title
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, 160)
            elide: Text.ElideRight
            text: {
                if (!root.hasPlayer) return "";
                let parts = [];
                if (root.currentPlayer.trackArtist)
                    parts.push(root.currentPlayer.trackArtist);
                if (root.currentPlayer.trackTitle)
                    parts.push(root.currentPlayer.trackTitle);
                return parts.join(" · ") || root.currentPlayer.identity || "";
            }
            color: "#1e1e2e"
            font.pixelSize: 11
        }

        // Previous
        Rectangle {
            width: 20; height: 20
            anchors.verticalCenter: parent.verticalCenter
            radius: 4
            color: prevArea.containsMouse ? "#45475a" : "transparent"
            visible: root.hasPlayer && root.currentPlayer.canGoPrevious

            Text {
                anchors.centerIn: parent
                text: "⏮"
                font.pixelSize: 10
                color: "#1e1e2e"
            }

            MouseArea {
                id: prevArea
                anchors.fill: parent
                hoverEnabled: true
                Accessible.role: Accessible.Button
                Accessible.name: "Previous track"
                onClicked: root.currentPlayer.previous()
            }
        }

        // Play / Pause
        Rectangle {
            width: 22; height: 22
            anchors.verticalCenter: parent.verticalCenter
            radius: 11
            color: playArea.containsMouse ? "#45475a" : "#313244"

            Text {
                anchors.centerIn: parent
                text: root.isPlaying ? "⏸" : "▶"
                font.pixelSize: 10
                color: "#cdd6f4"
            }

            MouseArea {
                id: playArea
                anchors.fill: parent
                hoverEnabled: true
                Accessible.role: Accessible.Button
                Accessible.name: root.isPlaying ? "Pause" : "Play"
                onClicked: if (root.hasPlayer) root.currentPlayer.togglePlaying()
            }
        }

        // Next
        Rectangle {
            width: 20; height: 20
            anchors.verticalCenter: parent.verticalCenter
            radius: 4
            color: nextArea.containsMouse ? "#45475a" : "transparent"
            visible: root.hasPlayer && root.currentPlayer.canGoNext

            Text {
                anchors.centerIn: parent
                text: "⏭"
                font.pixelSize: 10
                color: "#1e1e2e"
            }

            MouseArea {
                id: nextArea
                anchors.fill: parent
                hoverEnabled: true
                Accessible.role: Accessible.Button
                Accessible.name: "Next track"
                onClicked: root.currentPlayer.next()
            }
        }
    }
}
