// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.MediaPanel, the media chip's panel.
//
// Every MPRIS player on the bus, each with its own art, track and
// transport. The chip shows player 0 and toggles it; this is where the
// other players are, which is the whole reason the chip's right button
// opens something.
//
// Each transport button is gated on the player's own capability
// (canGoNext, canPause, ...). MPRIS players publish these and mean them:
// a radio stream has no previous track, and a button that looks live and
// does nothing is worse than one that is visibly unavailable.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.Mpris

PanelFrame {
    id: root

    title: qsTr("Media")
    iconName: "media-playback-start"
    subtitle: mprisHost.playerCount === 1 ? qsTr("1 player") : qsTr("%1 players").arg(mprisHost.playerCount)

    // Named mprisHost, not host: MprisPlayerModel has a `host` property of
    // its own, and `host: host` inside it would bind to itself.
    MprisHost {
        id: mprisHost
    }

    MprisPlayerModel {
        id: players

        host: mprisHost
    }

    Text {
        width: parent.width
        visible: players.count === 0
        text: qsTr("Nothing is playing")
        color: Theme.on_surface_variant
        font.pixelSize: Tokens.font_size_body_s
        font.family: Tokens.font_family_ui
        topPadding: Tokens.spacing_s
        bottomPadding: Tokens.spacing_s
    }

    Repeater {
        model: players

        delegate: Item {
            id: playerEntry

            required property var player

            readonly property bool _playing: playerEntry.player ? playerEntry.player.isPlaying : false
            readonly property bool _controllable: playerEntry.player ? playerEntry.player.canControl : false

            width: parent ? parent.width : 0
            implicitHeight: entryLayout.implicitHeight + Tokens.spacing_s

            RowLayout {
                id: entryLayout

                width: playerEntry.width
                spacing: Tokens.spacing_s

                // Album art when the player publishes a URL. Kirigami.Icon
                // takes a URL as readily as a theme name, so one item covers
                // both the art and the fallback glyph without a Loader.
                Kirigami.Icon {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    Layout.alignment: Qt.AlignVCenter
                    source: playerEntry.player && playerEntry.player.trackArtUrl !== "" ? playerEntry.player.trackArtUrl : "media-optical"
                    // Art is a picture and must not be recoloured; only the
                    // fallback glyph is a mask.
                    isMask: !(playerEntry.player && playerEntry.player.trackArtUrl !== "")
                    color: Theme.on_surface_variant
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Text {
                        Layout.fillWidth: true
                        text: playerEntry.player && playerEntry.player.trackTitle !== "" ? playerEntry.player.trackTitle : qsTr("Unknown track")
                        color: Theme.on_surface
                        font.pixelSize: Tokens.font_size_body_m
                        font.family: Tokens.font_family_ui
                        font.weight: playerEntry._playing ? Tokens.font_weight_medium : Tokens.font_weight_regular
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        // Artist when there is one, otherwise the player's
                        // own name, so the line is never blank and always
                        // says something about which row this is.
                        text: playerEntry.player && playerEntry.player.trackArtist !== "" ? playerEntry.player.trackArtist : (playerEntry.player ? playerEntry.player.identity : "")
                        color: Theme.on_surface_variant
                        font.pixelSize: Tokens.font_size_label_s
                        font.family: Tokens.font_family_ui
                        elide: Text.ElideRight
                    }
                }

                BarIconButton {
                    Layout.alignment: Qt.AlignVCenter
                    iconName: "media-skip-backward"
                    label: qsTr("Previous")
                    enabled: playerEntry.player !== null && playerEntry.player.canGoPrevious
                    onActivated: playerEntry.player.previous()
                }

                BarIconButton {
                    Layout.alignment: Qt.AlignVCenter
                    iconName: playerEntry._playing ? "media-playback-pause" : "media-playback-start"
                    label: playerEntry._playing ? qsTr("Pause") : qsTr("Play")
                    // canPause governs only the pause direction; a stopped
                    // player that can play must still offer the button.
                    enabled: playerEntry.player !== null && (playerEntry._playing ? playerEntry.player.canPause : playerEntry.player.canPlay)
                    onActivated: playerEntry.player.togglePlaying()
                }

                BarIconButton {
                    Layout.alignment: Qt.AlignVCenter
                    iconName: "media-skip-forward"
                    label: qsTr("Next")
                    enabled: playerEntry.player !== null && playerEntry.player.canGoNext
                    onActivated: playerEntry.player.next()
                }
            }

            // A press anywhere else on the row raises the player's window,
            // which is what someone looking at a list of players usually
            // wants next. This handler is on the row ITSELF while the
            // transport buttons are descendants, and Qt delivers a press to
            // the deepest item first, so pressing a button does not also
            // raise the window.
            TapHandler {
                enabled: playerEntry._controllable
                onTapped: {
                    if (playerEntry.player)
                        playerEntry.player.raise();
                }
            }
        }
    }
}
