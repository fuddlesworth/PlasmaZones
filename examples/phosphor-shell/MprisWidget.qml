// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Services 1.0
import Phosphor.Shell 1.0
import QtQuick
import QtQuick.Effects

// Media player capsule for the top panel. Shows album art (circular with
// progress ring), scrolling title, and prev/play/next controls. Left-click
// the capsule to open the MprisPopup detail panel; click the art circle
// to cycle players when multiple are running.
Item {
    // Middle-click = play/pause, scroll = volume.

    id: root

    property MprisPlayer currentPlayer: null
    readonly property bool hasPlayer: playerState.hasPlayer
    readonly property bool isPlaying: playerState.isPlaying
    readonly property real progress: playerState.progress
    readonly property alias stableArtUrl: playerState.stableArtUrl
    // Exposed so shell.qml can anchor the popup to us.
    property alias popupAnchor: artContainer

    signal popupRequested()

    // ─── Player selection ────────────────────────────────────────────
    function selectPlayer() {
        let playing = null;
        let paused = null;
        for (let i = 0; i < mprisHost.playerCount; i++) {
            let p = mprisHost.playerAt(i);
            if (!p)
                continue;

            if (p.isPlaying) {
                playing = p;
                break;
            }
            if (p.playbackState === MprisPlayer.Paused && !paused)
                paused = p;

        }
        let next = playing || paused || (mprisHost.playerCount > 0 ? mprisHost.playerAt(0) : null);
        if (root.currentPlayer !== next)
            root.currentPlayer = next;

    }

    function cyclePlayer() {
        if (mprisHost.playerCount <= 1)
            return ;

        let idx = 0;
        for (let i = 0; i < mprisHost.playerCount; i++) {
            if (mprisHost.playerAt(i) === currentPlayer) {
                idx = i;
                break;
            }
        }
        root.currentPlayer = mprisHost.playerAt((idx + 1) % mprisHost.playerCount);
    }

    visible: hasPlayer
    implicitWidth: visible ? capsule.implicitWidth : 0
    implicitHeight: parent ? parent.height : 30
    Component.onCompleted: selectPlayer()

    // Shared MPRIS derived-state + flicker-free art URL (see
    // MprisPlayerState.qml). `sampling` is left at its default true —
    // the progress ring's own Canvas culls repaints by visibility.
    MprisPlayerState {
        id: playerState

        player: root.currentPlayer
    }

    MprisHost {
        id: mprisHost
    }

    MprisPlayerModel {
        id: playerModel

        host: mprisHost
    }

    Connections {
        function onPlayerAdded() {
            root.selectPlayer();
        }

        function onPlayerRemoved() {
            root.selectPlayer();
        }

        function onPlayerCountChanged() {
            root.selectPlayer();
        }

        target: mprisHost
    }

    Connections {
        function onPlaybackStateChanged() {
            root.selectPlayer();
        }

        target: root.currentPlayer
        enabled: root.currentPlayer !== null
    }

    // ─── Capsule layout ──────────────────────────────────────────────
    Row {
        id: capsule

        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        // Album art with progress ring
        Item {
            id: artContainer

            width: 26
            height: 26
            anchors.verticalCenter: parent.verticalCenter

            // Background fill (visible while art loads / when no art).
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "#313244"
            }

            // Fallback glyph (sits underneath the masked art).
            Text {
                anchors.centerIn: parent
                text: "♪"
                color: "#a6adc8"
                font.pixelSize: 10
                visible: !artImage.visible
            }

            // Circular mask source for MultiEffect. Lives off-screen
            // (visible: false + hideSource: true). White circle on
            // transparent background — only the circle composites
            // through to the visible scene.
            Item {
                id: artMaskShape

                width: artContainer.width
                height: artContainer.height
                visible: false
                layer.enabled: true
                layer.smooth: true

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "white"
                }

            }

            // Album art masked to the circle via MultiEffect.maskSource
            // (QtQuick.Effects, Qt 6.5+). Qt6 has no built-in "rounded
            // clip" — Rectangle.radius is paint-only, clip: true is
            // bounding-box only. MultiEffect is the canonical mask path.
            Image {
                id: artImage

                anchors.fill: parent
                source: root.stableArtUrl
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(80, 80)
                asynchronous: true
                cache: true
                visible: status === Image.Ready || (source !== "" && status === Image.Loading)
                layer.enabled: true
                layer.smooth: true

                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: artMaskShape
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1
                }

            }

            // Progress ring drawn ON TOP of the art so the outer ring
            // stroke overlays the artwork edge.
            Canvas {
                id: progressRing

                // Only sample progress while the widget is visible and we
                // have a player. When there's no player or the widget is
                // hidden (occluded/no-player branch), keep `prog` at 0
                // and skip the per-second requestPaint cycle that would
                // otherwise re-rasterize the ring once per MPRIS position
                // tick regardless of visibility.
                property real prog: (root.visible && root.hasPlayer) ? root.progress : 0

                anchors.fill: parent
                z: 1
                onProgChanged: {
                    if (root.visible) {
                        requestPaint();
                    }
                }
                onPaint: {
                    let ctx = getContext("2d");
                    let cx = width / 2, cy = height / 2;
                    let r = Math.min(width, height) / 2 - 1;
                    ctx.reset();
                    ctx.beginPath();
                    ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                    ctx.lineWidth = 2;
                    ctx.strokeStyle = "#40585b70";
                    ctx.stroke();
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

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                Accessible.role: Accessible.Button
                Accessible.name: "Media player controls"
                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton)
                        root.cyclePlayer();
                    else
                        root.popupRequested();
                }
            }

        }

        // Scrolling title — left-click opens popup
        Item {
            width: 120
            height: parent.height
            anchors.verticalCenter: parent.verticalCenter
            clip: true

            MouseArea {
                anchors.fill: parent
                // hoverEnabled is required for cursorShape to take
                // effect — without it Qt only sets the cursor on
                // press, not on hover. The pointer-hand cue is the
                // primary affordance signaling that the title strip
                // opens the media popup.
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.popupRequested()
            }

            Text {
                id: titleText

                property bool needsScroll: implicitWidth > 120

                y: (parent.height - height) / 2
                text: {
                    if (!root.hasPlayer)
                        return "";

                    let parts = [];
                    if (root.currentPlayer.trackArtist)
                        parts.push(root.currentPlayer.trackArtist);

                    if (root.currentPlayer.trackTitle)
                        parts.push(root.currentPlayer.trackTitle);

                    return parts.join(" · ") || root.currentPlayer.identity || "";
                }
                color: "#1e1e2e"
                font.pixelSize: 11

                SequentialAnimation on x {
                    running: titleText.needsScroll && root.visible
                    loops: Animation.Infinite

                    PauseAnimation {
                        duration: 2000
                    }

                    NumberAnimation {
                        from: 0
                        to: -(titleText.implicitWidth - 110)
                        duration: titleText.implicitWidth * 25
                        easing.type: Easing.Linear
                    }

                    PauseAnimation {
                        duration: 1500
                    }

                    NumberAnimation {
                        from: -(titleText.implicitWidth - 110)
                        to: 0
                        duration: 400
                        easing.type: Easing.OutQuad
                    }

                }

            }

        }

        // Controls
        Row {
            spacing: 2
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                width: 20
                height: 20
                radius: 4
                color: prevArea.containsMouse ? "#45475a" : "transparent"
                visible: root.hasPlayer && root.currentPlayer.canGoPrevious

                Text {
                    anchors.centerIn: parent
                    text: "⏮"
                    font.pixelSize: 9
                    color: "#1e1e2e"
                }

                MouseArea {
                    id: prevArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: "Previous track"
                    onClicked: root.currentPlayer.previous()
                }

            }

            Rectangle {
                width: 22
                height: 22
                radius: 11
                color: playArea.containsMouse ? "#45475a" : "#313244"

                Text {
                    anchors.centerIn: parent
                    text: root.isPlaying ? "⏸" : "▶"
                    font.pixelSize: 9
                    color: "#cdd6f4"
                }

                MouseArea {
                    id: playArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: root.isPlaying ? "Pause" : "Play"
                    onClicked: {
                        if (root.hasPlayer) {
                            root.currentPlayer.togglePlaying();
                        }
                    }
                }

            }

            Rectangle {
                width: 20
                height: 20
                radius: 4
                color: nextArea.containsMouse ? "#45475a" : "transparent"
                visible: root.hasPlayer && root.currentPlayer.canGoNext

                Text {
                    anchors.centerIn: parent
                    text: "⏭"
                    font.pixelSize: 9
                    color: "#1e1e2e"
                }

                MouseArea {
                    id: nextArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: "Next track"
                    onClicked: root.currentPlayer.next()
                }

            }

        }

    }

    // This MouseArea is anchors.fill: capsule and is declared AFTER
    // the per-element MouseAreas inside the Row, which puts it on top
    // of them in stacking order. Even though acceptedButtons filters
    // it to MiddleButton for click events, Qt's cursor-shape lookup
    // walks the topmost MouseArea regardless of acceptedButtons —
    // so without an explicit cursorShape here this MouseArea's
    // default Qt.ArrowCursor would override the PointingHandCursor
    // set on the title-strip MouseArea below it (the art-container
    // MouseArea wins because it lives INSIDE artContainer, a deeper
    // child, but the title strip's MouseArea is at the same level
    // as this overlay and loses the stack). Set the shape here so
    // the entire capsule reads as clickable.
    MouseArea {
        anchors.fill: capsule
        acceptedButtons: Qt.MiddleButton
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        propagateComposedEvents: true
        onClicked: {
            if (root.hasPlayer) {
                root.currentPlayer.togglePlaying();
            }
        }
        onWheel: (wheel) => {
            if (!root.hasPlayer)
                return ;

            let delta = wheel.angleDelta.y > 0 ? 0.05 : -0.05;
            root.currentPlayer.volume = Math.max(0, Math.min(1, root.currentPlayer.volume + delta));
        }
    }

}
