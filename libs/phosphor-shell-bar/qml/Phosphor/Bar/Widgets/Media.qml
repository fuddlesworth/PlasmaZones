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

Item {
    id: root

    readonly property int maxTitleWidth: 200

    MprisHost {
        id: host
    }

    // Re-evaluated when playerCount changes; the metadata properties below
    // carry their own NOTIFY so the readout tracks the current track.
    readonly property var player: host.playerCount > 0 ? host.playerAt(0) : null
    readonly property string trackTitle: root.player ? root.player.trackTitle : ""
    readonly property string trackArtist: root.player ? root.player.trackArtist : ""
    readonly property bool isPlaying: root.player ? root.player.isPlaying : false

    visible: root.player !== null && (root.trackTitle.length > 0 || root.trackArtist.length > 0)
    implicitWidth: visible ? row.implicitWidth : 0
    implicitHeight: row.implicitHeight

    Accessible.role: Accessible.Indicator
    Accessible.name: (root.isPlaying ? "Playing " : "Paused ") + root.trackArtist + " " + root.trackTitle

    Row {
        id: row

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
            text: root.trackArtist.length > 0 ? root.trackArtist + " · " + root.trackTitle : root.trackTitle
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
        cursorShape: Qt.PointingHandCursor
        enabled: root.player !== null
        onClicked: if (root.player)
            root.player.togglePlaying()
    }
}
