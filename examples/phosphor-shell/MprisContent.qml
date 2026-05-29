// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Service.Mpris 1.0
import Phosphor.Theme
import QtQuick

// MPRIS content: body of the media panel popup. Wrapped by
// PanelPopupHost.
Item {
    id: root

    required property MprisPlayer currentPlayer
    property bool active: false
    readonly property bool hasPlayer: playerState.hasPlayer
    readonly property bool isPlaying: playerState.isPlaying
    readonly property real progress: playerState.progress
    readonly property alias stableArtUrl: playerState.stableArtUrl

    // Centralised metrics. See MprisWidget.qml for the rationale.
    readonly property int artSize: 160
    readonly property int prevNextSize: 32
    readonly property int playSize: 44
    readonly property int sectionSpacing: 10
    readonly property int labelStackSpacing: 2
    readonly property int controlGroupSpacing: 20
    readonly property int seekBarHeight: 24
    readonly property int seekTrackHeight: 4
    readonly property int identitySize: 10
    readonly property int titleSize: 14
    readonly property int subtitleSize: 11
    readonly property int seekTextSize: 9
    readonly property int fallbackGlyphSize: 42
    readonly property int controlGlyphSize: 14
    readonly property int playGlyphSize: 18
    readonly property int artRadius: 14
    readonly property int controlRadius: 8
    readonly property int seekRadius: 2
    readonly property int artSourcePixels: 320
    readonly property int popupScaleAnimMs: 300
    readonly property int popupOpacityAnimMs: 200

    function fmt(secs) {
        return playerState.fmt(secs);
    }

    scale: active ? 1 : 0.85
    opacity: active ? 1 : 0

    // Shared MPRIS derived-state + flicker-free art URL (see
    // MprisPlayerState.qml). `sampling` is gated on `active` so the 1 Hz
    // position binding stays asleep while the popup is closed: the seek
    // bar and time labels are off-screen then anyway.
    MprisPlayerState {
        id: playerState

        player: root.currentPlayer
        sampling: root.active
    }

    Column {
        anchors.fill: parent
        spacing: root.sectionSpacing

        Text {
            width: parent.width
            text: root.hasPlayer ? root.currentPlayer.identity : ""
            color: Theme.on_surface
            font.pixelSize: root.identitySize
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Rectangle {
            width: root.artSize
            height: root.artSize
            anchors.horizontalCenter: parent.horizontalCenter
            radius: root.artRadius
            color: Theme.surface_container
            clip: true

            Image {
                id: popupArt

                anchors.fill: parent
                source: root.stableArtUrl
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(root.artSourcePixels, root.artSourcePixels)
                mipmap: true
                smooth: true
                asynchronous: true
                cache: true
                visible: status === Image.Ready || (source !== "" && status === Image.Loading)
            }

            Text {
                anchors.centerIn: parent
                text: "♪"
                color: Theme.on_surface_variant
                font.pixelSize: root.fallbackGlyphSize
                visible: !popupArt.visible
            }
        }

        Column {
            width: parent.width
            spacing: root.labelStackSpacing

            Text {
                width: parent.width
                text: root.hasPlayer ? (root.currentPlayer.trackTitle || "") : ""
                color: Theme.on_surface
                font.pixelSize: root.titleSize
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

                    return p.join(" · ");
                }
                color: Theme.on_surface_variant
                font.pixelSize: root.subtitleSize
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }

        Item {
            width: parent.width
            height: root.seekBarHeight
            // Cull the seek bar entirely while the popup is closed.
            // `position` text otherwise re-renders once per second from
            // the 1Hz MPRIS position tick even when nothing's visible.
            visible: root.active && root.hasPlayer && root.currentPlayer.length > 0

            Text {
                anchors.left: parent.left
                anchors.top: parent.top
                text: root.hasPlayer ? root.fmt(root.currentPlayer.position) : ""
                color: Theme.on_surface_variant
                font.pixelSize: root.seekTextSize
            }

            Text {
                anchors.right: parent.right
                anchors.top: parent.top
                text: root.hasPlayer ? root.fmt(root.currentPlayer.length) : ""
                color: Theme.on_surface_variant
                font.pixelSize: root.seekTextSize
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: root.seekTrackHeight
                radius: root.seekRadius
                color: Theme.surface_container_high

                Rectangle {
                    width: parent.width * root.progress
                    height: parent.height
                    radius: root.seekRadius
                    color: Theme.on_surface
                }
            }

            MouseArea {
                anchors.fill: parent
                enabled: root.hasPlayer && root.currentPlayer.canSeek
                onClicked: mouse => {
                    // Defensive: don't depend on the enclosing Item's
                    // `visible` (length > 0) clause to keep this safe.
                    // Also guard width<=0: a transient layout pass with
                    // zero width would make `mouse.x / width` NaN or
                    // Infinity and setPosition(NaN) would push garbage
                    // onto the MPRIS wire.
                    if (!root.hasPlayer || root.currentPlayer.length <= 0 || width <= 0)
                        return;

                    let ratio = mouse.x / width;
                    let target = ratio * root.currentPlayer.length;
                    root.currentPlayer.setPosition(target);
                }
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.controlGroupSpacing

            Rectangle {
                width: root.prevNextSize
                height: root.prevNextSize
                radius: root.controlRadius
                color: pp.containsMouse ? Theme.surface_container_high : "transparent"
                visible: root.hasPlayer && root.currentPlayer.canGoPrevious && root.currentPlayer.canControl

                Text {
                    anchors.centerIn: parent
                    text: "⏮"
                    font.pixelSize: root.controlGlyphSize
                    color: Theme.on_surface
                }

                // Accessible.* lives on the MouseArea (the element that
                // actually handles activation), not the visual Rectangle.
                MouseArea {
                    id: pp

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Previous track")
                    onClicked: {
                        if (root.hasPlayer)
                            root.currentPlayer.previous();
                    }
                }
            }

            Rectangle {
                width: root.playSize
                height: root.playSize
                radius: width / 2
                color: ppla.containsMouse ? Theme.surface_container_high : Theme.surface_container
                visible: root.hasPlayer && root.currentPlayer.canControl

                Text {
                    anchors.centerIn: parent
                    text: root.isPlaying ? "⏸" : "▶"
                    font.pixelSize: root.playGlyphSize
                    color: Theme.on_surface
                }

                MouseArea {
                    id: ppla

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: root.isPlaying ? qsTr("Pause") : qsTr("Play")
                    onClicked: {
                        if (root.hasPlayer)
                            root.currentPlayer.togglePlaying();
                    }
                }
            }

            Rectangle {
                width: root.prevNextSize
                height: root.prevNextSize
                radius: root.controlRadius
                color: pn.containsMouse ? Theme.surface_container_high : "transparent"
                visible: root.hasPlayer && root.currentPlayer.canGoNext && root.currentPlayer.canControl

                Text {
                    anchors.centerIn: parent
                    text: "⏭"
                    font.pixelSize: root.controlGlyphSize
                    color: Theme.on_surface
                }

                MouseArea {
                    id: pn

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Next track")
                    onClicked: {
                        if (root.hasPlayer)
                            root.currentPlayer.next();
                    }
                }
            }
        }
    }

    Behavior on scale {
        NumberAnimation {
            duration: root.popupScaleAnimMs
            easing.type: Easing.OutBack
            easing.overshoot: 1.2
        }
    }

    Behavior on opacity {
        NumberAnimation {
            duration: root.popupOpacityAnimMs
        }
    }
}
