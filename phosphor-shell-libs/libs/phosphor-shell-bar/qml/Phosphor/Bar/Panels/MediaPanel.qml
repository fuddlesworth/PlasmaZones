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
        color: Appearance.muted
        font.pixelSize: Tokens.font_size_body_s
        font.family: Tokens.font_family_ui
        topPadding: Tokens.spacing_s
        bottomPadding: Tokens.spacing_s
    }

    Repeater {
        model: players
        delegate: MediaCard {
            required property var model
            player: model.player
            width: parent ? parent.width : 0
        }
    }
}
