// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Services 1.0
import Phosphor.Shell 1.0
import QtQuick

// Compact MPRIS media controls for the top panel's right zone.
// Shows the currently playing track with prev/play-pause/next buttons.
// Hides when no MPRIS player is active. Inhibits idle while playing.
Item {
    id: root

    readonly property var activePlayer: {
        for (let i = 0; i < mprisHost.playerCount; i++) {
            let p = mprisHost.playerAt(i);
            if (p && p.isPlaying)
                return p;
        }
        return mprisHost.playerCount > 0 ? mprisHost.playerAt(0) : null;
    }

    visible: activePlayer !== null
    implicitWidth: visible ? mediaRow.implicitWidth : 0
    implicitHeight: parent ? parent.height : 26

    MprisHost {
        id: mprisHost
    }

    // Prevent screen blanking while music is playing.
    IdleInhibitor {
        surface: Window.window
        Component.onCompleted: {
            if (!IdleInhibitor.isSupported())
                console.log("IdleInhibitor: protocol not available");
        }
    }

    Row {
        id: mediaRow

        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        // Track info — artist · title, truncated.
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, 140)
            elide: Text.ElideRight
            text: {
                if (!root.activePlayer)
                    return "";
                let parts = [];
                if (root.activePlayer.trackArtist)
                    parts.push(root.activePlayer.trackArtist);
                if (root.activePlayer.trackTitle)
                    parts.push(root.activePlayer.trackTitle);
                return parts.join(" · ") || root.activePlayer.identity || "";
            }
            color: "#1e1e2e"
            font.pixelSize: 11
        }

        // Prev
        Rectangle {
            width: 20
            height: 20
            anchors.verticalCenter: parent.verticalCenter
            radius: 4
            color: prevArea.containsMouse ? "#45475a" : "transparent"
            visible: root.activePlayer && root.activePlayer.canGoPrevious

            Text {
                anchors.centerIn: parent
                text: "⏮"
                font.pixelSize: 10
                color: "#1e1e2e"
            }

            MouseArea {
                id: prevArea
                anchors.fill: parent
                hoverEnabled: true
                Accessible.role: Accessible.Button
                Accessible.name: "Previous track"
                onClicked: if (root.activePlayer) root.activePlayer.previous()
            }
        }

        // Play / Pause
        Rectangle {
            width: 22
            height: 22
            anchors.verticalCenter: parent.verticalCenter
            radius: 11
            color: playArea.containsMouse ? "#45475a" : "#313244"

            Text {
                anchors.centerIn: parent
                text: root.activePlayer && root.activePlayer.isPlaying ? "⏸" : "▶"
                font.pixelSize: 10
                color: "#cdd6f4"
            }

            MouseArea {
                id: playArea
                anchors.fill: parent
                hoverEnabled: true
                Accessible.role: Accessible.Button
                Accessible.name: root.activePlayer && root.activePlayer.isPlaying ? "Pause" : "Play"
                onClicked: if (root.activePlayer) root.activePlayer.togglePlaying()
            }
        }

        // Next
        Rectangle {
            width: 20
            height: 20
            anchors.verticalCenter: parent.verticalCenter
            radius: 4
            color: nextArea.containsMouse ? "#45475a" : "transparent"
            visible: root.activePlayer && root.activePlayer.canGoNext

            Text {
                anchors.centerIn: parent
                text: "⏭"
                font.pixelSize: 10
                color: "#1e1e2e"
            }

            MouseArea {
                id: nextArea
                anchors.fill: parent
                hoverEnabled: true
                Accessible.role: Accessible.Button
                Accessible.name: "Next track"
                onClicked: if (root.activePlayer) root.activePlayer.next()
            }
        }
    }
}
