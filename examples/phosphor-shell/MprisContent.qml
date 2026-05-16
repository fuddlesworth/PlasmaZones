// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Services 1.0
import QtQuick

// MPRIS content — body of the media panel popup. Wrapped by
// PanelPopupHost.
Item {
    id: root

    required property var shellState
    required property MprisPlayer currentPlayer
    property bool active: false
    readonly property bool hasPlayer: playerState.hasPlayer
    readonly property bool isPlaying: playerState.isPlaying
    readonly property real progress: playerState.progress
    readonly property alias stableArtUrl: playerState.stableArtUrl

    function fmt(secs) {
        return playerState.fmt(secs);
    }

    scale: active ? 1 : 0.85
    opacity: active ? 1 : 0

    // Shared MPRIS derived-state + flicker-free art URL (see
    // MprisPlayerState.qml). `sampling` is gated on `active` so the 1 Hz
    // position binding stays asleep while the popup is closed — the seek
    // bar and time labels are off-screen then anyway.
    MprisPlayerState {
        id: playerState

        player: root.currentPlayer
        sampling: root.active
    }

    Column {
        anchors.fill: parent
        spacing: 10

        Text {
            width: parent.width
            text: root.hasPlayer ? root.currentPlayer.identity : ""
            color: "#1e1e2e"
            font.pixelSize: 10
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Rectangle {
            width: 160
            height: 160
            anchors.horizontalCenter: parent.horizontalCenter
            radius: 14
            color: "#20000000"
            clip: true

            Image {
                id: popupArt

                anchors.fill: parent
                source: root.stableArtUrl
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(320, 320)
                mipmap: true
                smooth: true
                asynchronous: true
                cache: true
                visible: status === Image.Ready || (source !== "" && status === Image.Loading)
            }

            Text {
                anchors.centerIn: parent
                text: "♪"
                color: "#45475a"
                font.pixelSize: 42
                visible: !popupArt.visible
            }

        }

        Column {
            width: parent.width
            spacing: 2

            Text {
                width: parent.width
                text: root.hasPlayer ? (root.currentPlayer.trackTitle || "") : ""
                color: "#1e1e2e"
                font.pixelSize: 14
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: {
                    if (!root.hasPlayer)
                        return "";

                    let p = [];
                    if (root.currentPlayer.trackArtist)
                        p.push(root.currentPlayer.trackArtist);

                    if (root.currentPlayer.trackAlbum)
                        p.push(root.currentPlayer.trackAlbum);

                    return p.join(" — ");
                }
                color: "#45475a"
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

        }

        Item {
            width: parent.width
            height: 24
            // Cull the seek bar entirely while the popup is closed —
            // `position` text otherwise re-renders once per second from
            // the 1Hz MPRIS position tick even when nothing's visible.
            visible: root.active && root.hasPlayer && root.currentPlayer.length > 0

            Text {
                anchors.left: parent.left
                anchors.top: parent.top
                text: root.hasPlayer ? root.fmt(root.currentPlayer.position) : ""
                color: "#45475a"
                font.pixelSize: 9
            }

            Text {
                anchors.right: parent.right
                anchors.top: parent.top
                text: root.hasPlayer ? root.fmt(root.currentPlayer.length) : ""
                color: "#45475a"
                font.pixelSize: 9
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 4
                radius: 2
                color: "#30000000"

                Rectangle {
                    width: parent.width * root.progress
                    height: parent.height
                    radius: 2
                    color: "#1e1e2e"
                }

            }

            MouseArea {
                anchors.fill: parent
                enabled: root.hasPlayer && root.currentPlayer.canSeek
                onClicked: (mouse) => {
                    // Defensive: don't depend on the enclosing Item's
                    // `visible` (length > 0) clause to keep this safe.
                    if (!root.hasPlayer || root.currentPlayer.length <= 0)
                        return ;

                    let ratio = mouse.x / width;
                    let target = ratio * root.currentPlayer.length;
                    root.currentPlayer.setPosition(target);
                }
            }

        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 20

            Rectangle {
                width: 32
                height: 32
                radius: 8
                color: pp.containsMouse ? "#30000000" : "transparent"
                visible: root.hasPlayer && root.currentPlayer.canGoPrevious && root.currentPlayer.canControl

                Text {
                    anchors.centerIn: parent
                    text: "⏮"
                    font.pixelSize: 14
                    color: "#1e1e2e"
                }

                // Accessible.* lives on the MouseArea — the element that
                // actually handles activation — not the visual Rectangle.
                MouseArea {
                    id: pp

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: "Previous track"
                    onClicked: root.currentPlayer.previous()
                }

            }

            Rectangle {
                width: 44
                height: 44
                radius: 22
                color: ppla.containsMouse ? "#30000000" : "#20000000"
                visible: root.hasPlayer && root.currentPlayer.canControl

                Text {
                    anchors.centerIn: parent
                    text: root.isPlaying ? "⏸" : "▶"
                    font.pixelSize: 18
                    color: "#1e1e2e"
                }

                MouseArea {
                    id: ppla

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: root.isPlaying ? "Pause" : "Play"
                    onClicked: {
                        if (root.hasPlayer)
                            root.currentPlayer.togglePlaying();

                    }
                }

            }

            Rectangle {
                width: 32
                height: 32
                radius: 8
                color: pn.containsMouse ? "#30000000" : "transparent"
                visible: root.hasPlayer && root.currentPlayer.canGoNext && root.currentPlayer.canControl

                Text {
                    anchors.centerIn: parent
                    text: "⏭"
                    font.pixelSize: 14
                    color: "#1e1e2e"
                }

                MouseArea {
                    id: pn

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

    Behavior on scale {
        NumberAnimation {
            duration: 300
            easing.type: Easing.OutBack
            easing.overshoot: 1.2
        }

    }

    Behavior on opacity {
        NumberAnimation {
            duration: 200
        }

    }

}
