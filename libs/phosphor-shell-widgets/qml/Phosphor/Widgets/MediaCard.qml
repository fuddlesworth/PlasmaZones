// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme

Rectangle {
    id: root
    property var player: null
    readonly property bool playing: player !== null && player.isPlaying
    readonly property bool controllable: player !== null && player.canControl
    implicitWidth: 320
    implicitHeight: content.implicitHeight + 28
    radius: Math.max(4, Appearance.radius - 5)
    color: Appearance.recess
    border.width: 1
    border.color: Appearance.outline

    ColumnLayout {
        id: content
        x: 14
        y: 14
        width: parent.width - 28
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                radius: Math.min(12, Appearance.radius)
                color: Qt.alpha(Appearance.accent, 0.15)
                Kirigami.Icon {
                    anchors.fill: parent
                    anchors.margins: root.player && root.player.trackArtUrl !== "" ? 0 : 12
                    source: root.player && root.player.trackArtUrl !== "" ? root.player.trackArtUrl : "media-optical"
                    isMask: !(root.player && root.player.trackArtUrl !== "")
                    color: Appearance.accent
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: root.player ? (root.player.trackTitle || root.player.identity) : qsTr("Nothing is playing")
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: root.player ? (root.player.trackArtist || root.player.identity) : qsTr("Open a music or video player")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }
        SpectrumVisualizer {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            visible: root.player !== null && Appearance.visualizer !== "off" && Appearance.media
            playing: root.playing
        }
        RowLayout {
            visible: root.player !== null
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: root.playing ? qsTr("Now playing") : qsTr("Paused")
                font.family: Tokens.font_family_ui
                font.pixelSize: 11
                color: Appearance.muted
            }
            ShellButton {
                iconName: "media-skip-backward"
                label: qsTr("Previous track")
                enabled: root.controllable && root.player.canGoPrevious
                onClicked: root.player.previous()
            }
            ShellButton {
                iconName: root.playing ? "media-playback-pause" : "media-playback-start"
                label: root.playing ? qsTr("Pause") : qsTr("Play")
                highlighted: true
                enabled: root.controllable && (root.playing ? root.player.canPause : root.player.canPlay)
                onClicked: root.player.togglePlaying()
            }
            ShellButton {
                iconName: "media-skip-forward"
                label: qsTr("Next track")
                enabled: root.controllable && root.player.canGoNext
                onClicked: root.player.next()
            }
        }
    }
}
