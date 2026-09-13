// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

ShellSurface {
    id: root
    property var player: null
    property var spectrum: AudioSpectrum
    readonly property bool controllable: player !== null && player.canControl
    implicitWidth: 402
    implicitHeight: 106
    clip: true
    RowLayout {
        x: 17
        y: 17
        width: parent.width - 34
        spacing: 10
        Rectangle {
            Layout.preferredWidth: 43
            Layout.preferredHeight: 46
            color: Qt.alpha(Appearance.accent, .15)
            radius: Appearance.radius * .35
            clip: true
            Image {
                anchors.fill: parent
                source: root.player ? root.player.trackArtUrl : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            ShellIcon {
                anchors.centerIn: parent
                visible: !root.player || !root.player.trackArtUrl
                source: "audio-x-generic"
                color: Appearance.accent
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                text: qsTr("NOW PLAYING")
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 7
                font.letterSpacing: 1.3
            }
            Text {
                Layout.fillWidth: true
                text: root.player ? (root.player.trackTitle || root.player.identity) : ""
                textFormat: Text.PlainText
                color: Appearance.text
                font.family: Tokens.font_family_ui
                font.pixelSize: 12
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: root.player ? (root.player.trackArtist || root.player.identity) : ""
                textFormat: Text.PlainText
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 9
                elide: Text.ElideRight
            }
        }
        ShellButton {
            objectName: "lockPrevious"
            implicitWidth: 27
            implicitHeight: 30
            flat: true
            iconName: "media-skip-backward"
            label: qsTr("Previous track")
            enabled: root.controllable && root.player.canGoPrevious
            onClicked: root.player.previous()
        }
        ShellButton {
            objectName: "lockPlay"
            implicitWidth: 30
            implicitHeight: 30
            cornerRadius: 15
            iconName: root.player && root.player.isPlaying ? "media-playback-pause" : "media-playback-start"
            label: root.player && root.player.isPlaying ? qsTr("Pause") : qsTr("Play")
            enabled: root.controllable && (root.player.isPlaying ? root.player.canPause : root.player.canPlay)
            onClicked: root.player.togglePlaying()
        }
        ShellButton {
            objectName: "lockNext"
            implicitWidth: 27
            implicitHeight: 30
            flat: true
            iconName: "media-skip-forward"
            label: qsTr("Next track")
            enabled: root.controllable && root.player.canGoNext
            onClicked: root.player.next()
        }
    }
    SpectrumVisualizer {
        x: 17
        width: parent.width - 34
        height: 32
        anchors.bottom: parent.bottom
        anchors.bottomMargin: -6
        opacity: .7
        spectrum: root.spectrum
        playing: root.player !== null && root.player.isPlaying
    }
}
