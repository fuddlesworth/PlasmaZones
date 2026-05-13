// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Services 1.0
import Phosphor.Shell 1.0
import QtQuick

// Media player detail popup — opens below the MprisWidget capsule.
// Uses the same frosted-glass look as the taskbar and calendar popup.
PopupWindow {
    id: root

    required property var shellState
    required property var anchorItem
    required property MprisPlayer currentPlayer

    readonly property bool hasPlayer: currentPlayer !== null
    readonly property bool isPlaying: hasPlayer && currentPlayer.isPlaying
    readonly property real progress: {
        if (!hasPlayer || currentPlayer.length <= 0) return 0;
        return Math.min(1.0, Math.max(0, currentPlayer.position / currentPlayer.length));
    }

    anchor: anchorItem
    popupEdge: PopupWindow.Below
    popupWidth: 300
    popupHeight: 360
    gap: 8
    popupVisible: false

    onPopupVisibleChanged: {
        if (!popupVisible && shellState.mediaOpen)
            shellState.mediaOpen = false;
    }

    Connections {
        function onMediaOpenChanged() { root.popupVisible = shellState.mediaOpen; }
        target: shellState
    }

    function fmt(secs) {
        if (isNaN(secs) || secs < 0) return "0:00";
        let m = Math.floor(secs / 60);
        let s = Math.floor(secs % 60);
        return m + ":" + (s < 10 ? "0" : "") + s;
    }

    // Same frosted-glass backdrop as the taskbar
    ShaderBackground {
        anchors.fill: parent
        playing: root.popupVisible
        shaderSource: Qt.resolvedUrl("shaders/frosted_glass.frag")
        shaderParams: {
            "customParams1_x": 0.8,
            "customParams1_y": 0.15,
            "customParams1_z": 20,
            "customParams1_w": 0.5,
            "customParams2_x": 14
        }
        customColor1: "#1e1e2e"
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"; radius: 14
        border.color: "#40a6adc8"; border.width: 1
    }

    Item {
        id: content
        anchors.fill: parent
        anchors.margins: 16
        scale: shellState.mediaOpen ? 1 : 0.85
        opacity: shellState.mediaOpen ? 1 : 0

        Column {
            anchors.fill: parent
            spacing: 10

            // Player identity
            Text {
                width: parent.width
                text: root.hasPlayer ? root.currentPlayer.identity : ""
                color: "#a6adc8"; font.pixelSize: 10; font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            // Large album art
            Rectangle {
                width: 160; height: 160
                anchors.horizontalCenter: parent.horizontalCenter
                radius: 14; color: "#313244"; clip: true

                Image {
                    id: popupArt
                    anchors.fill: parent
                    source: root.hasPlayer ? (root.currentPlayer.trackArtUrl || "") : ""
                    fillMode: Image.PreserveAspectCrop
                    sourceSize: Qt.size(320, 320)
                    asynchronous: true
                    cache: true
                }

                Text {
                    anchors.centerIn: parent
                    text: "♪"; color: "#585b70"; font.pixelSize: 42
                    visible: popupArt.status !== Image.Ready
                }
            }

            // Track info
            Column {
                width: parent.width; spacing: 2

                Text {
                    width: parent.width
                    text: root.hasPlayer ? (root.currentPlayer.trackTitle || "") : ""
                    color: "#cdd6f4"; font.pixelSize: 14; font.weight: Font.Bold
                    horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: {
                        if (!root.hasPlayer) return "";
                        let p = [];
                        if (root.currentPlayer.trackArtist) p.push(root.currentPlayer.trackArtist);
                        if (root.currentPlayer.trackAlbum) p.push(root.currentPlayer.trackAlbum);
                        return p.join(" — ");
                    }
                    color: "#a6adc8"; font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                }
            }

            // Seek bar + time
            Item {
                width: parent.width; height: 24
                visible: root.hasPlayer && root.currentPlayer.length > 0

                Text {
                    anchors.left: parent.left; anchors.top: parent.top
                    text: root.hasPlayer ? root.fmt(root.currentPlayer.position) : ""
                    color: "#6c7086"; font.pixelSize: 9
                }
                Text {
                    anchors.right: parent.right; anchors.top: parent.top
                    text: root.hasPlayer ? root.fmt(root.currentPlayer.length) : ""
                    color: "#6c7086"; font.pixelSize: 9
                }

                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 4; radius: 2; color: "#45475a"

                    Rectangle {
                        width: parent.width * root.progress
                        height: parent.height; radius: 2; color: "#89b4fa"
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: root.hasPlayer && root.currentPlayer.canSeek
                    onClicked: (mouse) => {
                        let ratio = mouse.x / width;
                        let target = ratio * root.currentPlayer.length;
                        root.currentPlayer.seek(target - root.currentPlayer.position);
                    }
                }
            }

            // Controls
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 20

                Rectangle {
                    width: 32; height: 32; radius: 8
                    color: pp.containsMouse ? "#45475a" : "transparent"
                    visible: root.hasPlayer && root.currentPlayer.canGoPrevious
                    Text { anchors.centerIn: parent; text: "⏮"; font.pixelSize: 14; color: "#cdd6f4" }
                    MouseArea { id: pp; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.currentPlayer.previous() }
                }

                Rectangle {
                    width: 44; height: 44; radius: 22
                    color: ppla.containsMouse ? "#45475a" : "#313244"
                    Text { anchors.centerIn: parent; text: root.isPlaying ? "⏸" : "▶"; font.pixelSize: 18; color: "#cdd6f4" }
                    MouseArea { id: ppla; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: if (root.hasPlayer) root.currentPlayer.togglePlaying() }
                }

                Rectangle {
                    width: 32; height: 32; radius: 8
                    color: pn.containsMouse ? "#45475a" : "transparent"
                    visible: root.hasPlayer && root.currentPlayer.canGoNext
                    Text { anchors.centerIn: parent; text: "⏭"; font.pixelSize: 14; color: "#cdd6f4" }
                    MouseArea { id: pn; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.currentPlayer.next() }
                }
            }
        }

        Behavior on scale { NumberAnimation { duration: 300; easing.type: Easing.OutBack; easing.overshoot: 1.2 } }
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }
}
