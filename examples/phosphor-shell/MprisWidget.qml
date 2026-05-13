// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Services 1.0
import Phosphor.Shell 1.0
import QtQuick

// Media player capsule for the top panel. Shows album art (circular with
// progress ring), scrolling title, and prev/play/next controls. Hides
// when no MPRIS player is active. Click the art to cycle players when
// multiple are running.
//
// Ported from the noctalia-shell MediaMini / MediaService pattern,
// adapted to Phosphor.Services types.
Item {
    id: root

    property MprisPlayer currentPlayer: null
    readonly property bool hasPlayer: currentPlayer !== null
    readonly property bool isPlaying: hasPlayer && currentPlayer.isPlaying
    readonly property real progress: {
        if (!hasPlayer || currentPlayer.length <= 0)
            return 0;
        return Math.min(1.0, Math.max(0, currentPlayer.position / currentPlayer.length));
    }

    visible: hasPlayer
    implicitWidth: visible ? capsule.implicitWidth : 0
    implicitHeight: parent ? parent.height : 30

    MprisHost { id: mprisHost }
    MprisPlayerModel { id: playerModel; host: mprisHost }

    // ─── Player selection ────────────────────────────────────────────
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
        if (root.currentPlayer !== next) root.currentPlayer = next;
    }

    function cyclePlayer() {
        if (mprisHost.playerCount <= 1) return;
        let idx = 0;
        for (let i = 0; i < mprisHost.playerCount; i++) {
            if (mprisHost.playerAt(i) === currentPlayer) { idx = i; break; }
        }
        root.currentPlayer = mprisHost.playerAt((idx + 1) % mprisHost.playerCount);
    }

    Connections {
        target: mprisHost
        function onPlayerAdded() { root.selectPlayer(); }
        function onPlayerRemoved() { root.selectPlayer(); }
        function onPlayerCountChanged() { root.selectPlayer(); }
    }
    Connections {
        target: root.currentPlayer
        enabled: root.currentPlayer !== null
        function onPlaybackStateChanged() { root.selectPlayer(); }
    }
    Component.onCompleted: selectPlayer()

    // ─── Capsule layout ──────────────────────────────────────────────
    Row {
        id: capsule
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        // Album art with progress ring
        Item {
            id: artContainer
            width: 26; height: 26
            anchors.verticalCenter: parent.verticalCenter
            visible: root.hasPlayer

            Canvas {
                id: progressRing
                anchors.fill: parent

                property real prog: root.progress
                onProgChanged: requestPaint()

                onPaint: {
                    let ctx = getContext("2d");
                    let cx = width / 2, cy = height / 2;
                    let r = Math.min(width, height) / 2 - 2;
                    ctx.reset();
                    // Background ring
                    ctx.beginPath();
                    ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                    ctx.lineWidth = 2;
                    ctx.strokeStyle = "#40585b70";
                    ctx.stroke();
                    // Progress arc
                    if (prog > 0) {
                        ctx.beginPath();
                        ctx.arc(cx, cy, r, -Math.PI / 2, -Math.PI / 2 + prog * 2 * Math.PI);
                        ctx.lineWidth = 2;
                        ctx.strokeStyle = "#89b4fa";
                        ctx.lineCap = "round";
                        ctx.stroke();
                    }
                }
            }

            // Album art image (clipped to circle)
            Rectangle {
                anchors.centerIn: parent
                width: 20; height: 20
                radius: 10
                color: "#313244"
                clip: true

                Image {
                    anchors.fill: parent
                    source: root.hasPlayer ? root.currentPlayer.trackArtUrl : ""
                    fillMode: Image.PreserveAspectCrop
                    visible: status === Image.Ready
                    sourceSize: Qt.size(40, 40)
                }

                // Fallback icon when no art
                Text {
                    anchors.centerIn: parent
                    text: "♪"
                    color: "#a6adc8"
                    font.pixelSize: 10
                    visible: !root.hasPlayer || root.currentPlayer.trackArtUrl === ""
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.Button
                Accessible.name: "Cycle media player"
                onClicked: root.cyclePlayer()
            }
        }

        // Scrolling title: "Artist · Title"
        Item {
            width: 120
            height: parent.height
            anchors.verticalCenter: parent.verticalCenter
            clip: true
            visible: root.hasPlayer

            Text {
                id: titleText
                y: (parent.height - height) / 2
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

                // Scroll when text overflows
                property bool needsScroll: implicitWidth > 120

                SequentialAnimation on x {
                    running: titleText.needsScroll && root.visible
                    loops: Animation.Infinite

                    PauseAnimation { duration: 2000 }
                    NumberAnimation {
                        from: 0
                        to: -(titleText.implicitWidth - 110)
                        duration: titleText.implicitWidth * 25
                        easing.type: Easing.Linear
                    }
                    PauseAnimation { duration: 1500 }
                    NumberAnimation {
                        from: -(titleText.implicitWidth - 110)
                        to: 0
                        duration: 400
                        easing.type: Easing.OutQuad
                    }
                }
            }
        }

        // Playback time
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.hasPlayer && root.currentPlayer.length > 0
            text: {
                if (!root.hasPlayer) return "";
                let pos = Math.floor(root.currentPlayer.position);
                let len = Math.floor(root.currentPlayer.length);
                let fmt = (s) => {
                    let m = Math.floor(s / 60);
                    let sec = Math.floor(s % 60);
                    return m + ":" + (sec < 10 ? "0" : "") + sec;
                };
                return fmt(pos) + "/" + fmt(len);
            }
            color: "#585b70"
            font.pixelSize: 9
        }

        // Controls
        Row {
            spacing: 2
            anchors.verticalCenter: parent.verticalCenter
            visible: root.hasPlayer

            // Previous
            Rectangle {
                width: 20; height: 20
                radius: 4
                color: prevArea.containsMouse ? "#45475a" : "transparent"
                visible: root.hasPlayer && root.currentPlayer.canGoPrevious

                Text {
                    anchors.centerIn: parent
                    text: "⏮"; font.pixelSize: 9; color: "#1e1e2e"
                }
                MouseArea {
                    id: prevArea; anchors.fill: parent
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: "Previous track"
                    onClicked: root.currentPlayer.previous()
                }
            }

            // Play / Pause
            Rectangle {
                width: 22; height: 22
                radius: 11
                color: playArea.containsMouse ? "#45475a" : "#313244"

                Text {
                    anchors.centerIn: parent
                    text: root.isPlaying ? "⏸" : "▶"
                    font.pixelSize: 9; color: "#cdd6f4"
                }
                MouseArea {
                    id: playArea; anchors.fill: parent
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: root.isPlaying ? "Pause" : "Play"
                    onClicked: if (root.hasPlayer) root.currentPlayer.togglePlaying()
                }
            }

            // Next
            Rectangle {
                width: 20; height: 20
                radius: 4
                color: nextArea.containsMouse ? "#45475a" : "transparent"
                visible: root.hasPlayer && root.currentPlayer.canGoNext

                Text {
                    anchors.centerIn: parent
                    text: "⏭"; font.pixelSize: 9; color: "#1e1e2e"
                }
                MouseArea {
                    id: nextArea; anchors.fill: parent
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: "Next track"
                    onClicked: root.currentPlayer.next()
                }
            }
        }
    }

    // Middle-click anywhere = play/pause, scroll = volume
    MouseArea {
        anchors.fill: capsule
        acceptedButtons: Qt.MiddleButton
        propagateComposedEvents: true
        onClicked: if (root.hasPlayer) root.currentPlayer.togglePlaying()
        onWheel: (wheel) => {
            if (!root.hasPlayer) return;
            let delta = wheel.angleDelta.y > 0 ? 0.05 : -0.05;
            root.currentPlayer.setVolume(Math.max(0, Math.min(1, root.currentPlayer.volume + delta)));
        }
    }
}
