// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Services 1.0
import Phosphor.Shell 1.0
import QtQuick

// Media player detail popup — frosted glass shader tinted to match the
// panel's Catppuccin mauve palette. Avoids gradient.frag's screen-
// relative UV params (panelToScreenH) which produce wrong wallpaper
// sampling when the surface isn't at a known screen position.
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

    // Stable art URL — only updates when the actual URL string changes,
    // preventing Image reload flicker on unrelated metadataChanged signals.
    property string stableArtUrl: ""
    function _updateArtUrl() {
        let url = (hasPlayer && currentPlayer.trackArtUrl) ? currentPlayer.trackArtUrl : "";
        if (stableArtUrl !== url) stableArtUrl = url;
    }
    onCurrentPlayerChanged: _updateArtUrl()
    Connections {
        id: popupMetaConn
        target: root.currentPlayer
        enabled: root.currentPlayer !== null
        function onMetadataChanged() { root._updateArtUrl(); }
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

    // Frosted glass shader tinted to match the panel's mauve palette.
    // Uses frosted_glass.frag which is self-contained (no screen-relative
    // UV params), avoiding the wallpaper-sampling distortion that
    // gradient.frag produces when panelToScreenH is unknown.
    ShaderBackground {
        anchors.fill: parent
        playing: root.popupVisible
        shaderSource: Qt.resolvedUrl("shaders/frosted_glass.frag")
        shaderParams: {
            "customParams1_x": 0.85,
            "customParams1_y": 0.06,
            "customParams1_z": 24,
            "customParams1_w": 0.8,
            "customParams2_x": 14
        }
        customColor1: "#cba6f7"
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
                color: "#1e1e2e"; font.pixelSize: 10; font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            // Large album art
            Rectangle {
                width: 160; height: 160
                anchors.horizontalCenter: parent.horizontalCenter
                radius: 14; color: "#20000000"; clip: true

                Image {
                    id: popupArt
                    anchors.fill: parent
                    source: root.stableArtUrl
                    fillMode: Image.PreserveAspectCrop
                    sourceSize: Qt.size(320, 320)
                    asynchronous: true
                    cache: true
                    // Keep the last loaded image visible during Loading states
                    // to prevent flicker when metadata changes re-trigger load.
                    visible: status === Image.Ready || (source !== "" && status === Image.Loading)
                }

                Text {
                    anchors.centerIn: parent
                    text: "♪"; color: "#45475a"; font.pixelSize: 42
                    visible: !popupArt.visible
                }
            }

            // Track info
            Column {
                width: parent.width; spacing: 2

                Text {
                    width: parent.width
                    text: root.hasPlayer ? (root.currentPlayer.trackTitle || "") : ""
                    color: "#1e1e2e"; font.pixelSize: 14; font.weight: Font.Bold
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
                    color: "#45475a"; font.pixelSize: 11
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
                    color: "#45475a"; font.pixelSize: 9
                }
                Text {
                    anchors.right: parent.right; anchors.top: parent.top
                    text: root.hasPlayer ? root.fmt(root.currentPlayer.length) : ""
                    color: "#45475a"; font.pixelSize: 9
                }

                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 4; radius: 2; color: "#30000000"

                    Rectangle {
                        width: parent.width * root.progress
                        height: parent.height; radius: 2; color: "#1e1e2e"
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
                    color: pp.containsMouse ? "#30000000" : "transparent"
                    visible: root.hasPlayer && root.currentPlayer.canGoPrevious
                    Text { anchors.centerIn: parent; text: "⏮"; font.pixelSize: 14; color: "#1e1e2e" }
                    MouseArea { id: pp; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.currentPlayer.previous() }
                }

                Rectangle {
                    width: 44; height: 44; radius: 22
                    color: ppla.containsMouse ? "#30000000" : "#20000000"
                    Text { anchors.centerIn: parent; text: root.isPlaying ? "⏸" : "▶"; font.pixelSize: 18; color: "#1e1e2e" }
                    MouseArea { id: ppla; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: if (root.hasPlayer) root.currentPlayer.togglePlaying() }
                }

                Rectangle {
                    width: 32; height: 32; radius: 8
                    color: pn.containsMouse ? "#30000000" : "transparent"
                    visible: root.hasPlayer && root.currentPlayer.canGoNext
                    Text { anchors.centerIn: parent; text: "⏭"; font.pixelSize: 14; color: "#1e1e2e" }
                    MouseArea { id: pn; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.currentPlayer.next() }
                }
            }
        }

        Behavior on scale { NumberAnimation { duration: 300; easing.type: Easing.OutBack; easing.overshoot: 1.2 } }
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }
}
