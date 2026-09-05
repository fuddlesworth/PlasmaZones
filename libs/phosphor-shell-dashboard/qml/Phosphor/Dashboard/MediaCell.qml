// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Dashboard.MediaCell, the media cell in the dashboard's last
// row (A3 §7 b).
//
// Title, artist, a 2 px position band (SpectrumUnderline as a slider)
// and three 13 px text transport glyphs, in the desktop cell's outline.
// `host` is an MprisHost (or any object with `playerCount`,
// `playerAt(i)` and `playerCountChanged`); the cell follows the playing
// player, else the paused one, else the first. Without a host or a
// player the cell says so.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    property var host: null
    // The player on display. Re-picked on every player edge; a test can
    // hand one in directly.
    property var player: null

    function _pick(): void {
        if (!host) {
            player = null;
            return;
        }
        let playing = null;
        let paused = null;
        for (let i = 0; i < host.playerCount; ++i) {
            const p = host.playerAt(i);
            if (!p)
                continue;
            if (p.isPlaying && !playing)
                playing = p;
            else if (!p.isPlaying && !paused)
                paused = p;
        }
        player = playing || paused || (host.playerCount > 0 ? host.playerAt(0) : null);
    }

    Connections {
        target: root.host
        function onPlayerCountChanged() {
            root._pick();
        }
    }
    onHostChanged: _pick()
    Component.onCompleted: _pick()

    readonly property string title: player ? player.trackTitle : ""
    readonly property string artist: player ? player.trackArtist : ""
    readonly property real progress: player && player.length > 0 ? Math.max(0, Math.min(1, player.position / player.length)) : 0

    Accessible.role: Accessible.StaticText
    Accessible.name: player ? qsTr("Media, %1 by %2").arg(title).arg(artist) : qsTr("Media, nothing playing")

    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_edge
        color: "transparent"
        border.width: 1
        border.color: Spectrum.resting
    }

    TabularText {
        id: label

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Tokens.spacing_s
        text: qsTr("Media")
        font.pixelSize: Tokens.font_size_label_m
        color: Theme.on_surface_variant
    }

    TabularText {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Tokens.spacing_s
        visible: root.player !== null
        text: root.player ? root.player.identity : ""
        font.pixelSize: Tokens.font_size_label_m
        color: Theme.on_surface_variant
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Tokens.spacing_l
        spacing: Tokens.spacing_xs

        Text {
            width: parent.width
            text: root.player ? (root.title !== "" ? root.title : qsTr("Untitled")) : qsTr("Nothing playing")
            color: Theme.on_surface
            font.family: Tokens.font_family_ui
            font.pixelSize: Tokens.font_size_title_s
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: root.artist !== ""
            text: root.artist
            color: Theme.on_surface_variant
            font.family: Tokens.font_family_ui
            font.pixelSize: Tokens.font_size_body_m
            elide: Text.ElideRight
        }

        // The 2 px position band over a resting track.
        Item {
            width: parent.width
            height: 2
            Rectangle {
                anchors.fill: parent
                color: Theme.on_surface
                opacity: 0.14
            }
            SpectrumUnderline {
                id: band

                length: parent.width
                value: root.progress
                t: 0.35
            }
        }

        // Transport as text glyphs (A3 §7 b: no icon art).
        Row {
            spacing: Tokens.spacing_l
            visible: root.player !== null

            Repeater {
                model: [
                    {
                        "glyph": "⏮",
                        "name": qsTr("Previous"),
                        "enabled": root.player ? root.player.canGoPrevious : false,
                        "act": () => root.player.previous()
                    },
                    {
                        "glyph": root.player && root.player.isPlaying ? "⏸" : "⏵",
                        "name": root.player && root.player.isPlaying ? qsTr("Pause") : qsTr("Play"),
                        "enabled": root.player ? (root.player.canPlay || root.player.canPause) : false,
                        "act": () => root.player.togglePlaying()
                    },
                    {
                        "glyph": "⏭",
                        "name": qsTr("Next"),
                        "enabled": root.player ? root.player.canGoNext : false,
                        "act": () => root.player.next()
                    }
                ]
                delegate: Text {
                    id: glyph

                    required property var modelData

                    text: modelData.glyph
                    color: Theme.on_surface
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Tokens.font_size_label_l
                    opacity: !modelData.enabled ? 0.35 : glyphHover.hovered ? 1 : 0.8

                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.name

                    HoverHandler {
                        id: glyphHover

                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        enabled: glyph.modelData.enabled
                        onTapped: glyph.modelData.act()
                    }
                }
            }
        }
    }
}
