// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Effects
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme

Rectangle {
    id: root
    property var player: null
    property var spectrum: AudioSpectrum
    property bool shelf: false
    readonly property bool playing: player !== null && player.isPlaying
    readonly property bool controllable: player !== null && player.canControl
    readonly property int inset: shelf ? 18 : 15
    implicitWidth: 320
    implicitHeight: Math.max(shelf ? 246 : 223, content.implicitHeight + inset * 2 + (shelf ? 2 : 0))
    radius: Appearance.radius * 0.65
    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop {
            position: 0
            color: Qt.tint(Appearance.recess, Qt.alpha(Appearance.stops[1], 0.18))
        }
        GradientStop {
            position: 1
            color: Qt.tint(Appearance.recess, Qt.alpha(Appearance.stops[3], 0.1))
        }
    }
    border.width: 1
    border.color: Appearance.outline
    ColumnLayout {
        id: content
        x: root.inset
        y: root.inset
        width: parent.width - root.inset * 2
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle {
                id: album
                Layout.preferredWidth: root.shelf ? 58 : 50
                Layout.preferredHeight: root.shelf ? 64 : 55
                radius: 7
                color: Qt.alpha(Appearance.accent, 0.15)
                Rectangle {
                    id: coverMask
                    anchors.fill: parent
                    radius: 7
                    color: "white"
                    visible: false
                    layer.enabled: true
                }
                Image {
                    anchors.fill: parent
                    source: root.player ? root.player.trackArtUrl : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: status === Image.Ready
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: coverMask
                    }
                }
                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: 26
                    height: 26
                    visible: !root.player || !root.player.trackArtUrl
                    source: "media-optical"
                    isMask: true
                    color: Appearance.accent
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    text: root.player ? (root.player.trackTitle || root.player.identity) : qsTr("Nothing is playing")
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: root.shelf ? 15 : 12
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: root.player ? (root.player.trackArtist || root.player.identity) + (root.player.trackAlbum ? " · " + root.player.trackAlbum : "") : qsTr("Open a music or video player")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }
            ShellButton {
                implicitWidth: 33
                implicitHeight: 33
                cornerRadius: 17
                iconName: root.playing ? "media-playback-pause" : "media-playback-start"
                label: root.playing ? qsTr("Pause") : qsTr("Play")
                enabled: root.controllable && (root.playing ? root.player.canPause : root.player.canPlay)
                onClicked: root.player.togglePlaying()
            }
        }
        SpectrumVisualizer {
            Layout.fillWidth: true
            Layout.preferredHeight: root.shelf ? 112 : 90
            Layout.topMargin: root.shelf ? 8 : 10
            Layout.bottomMargin: root.shelf ? 5 : 0
            visible: Appearance.visualizer !== "off"
            playing: root.playing
            spectrum: root.spectrum
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Appearance.outline
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Text {
                Layout.fillWidth: true
                text: !root.player ? qsTr("No audio") : root.playing ? qsTr("System audio") : qsTr("Paused")
                font.family: Tokens.font_family_mono
                font.pixelSize: 9
                color: Appearance.muted
            }
            ShellComboBox {
                implicitWidth: 78
                implicitHeight: 26
                labelSize: 9
                model: [qsTr("Ribbon"), qsTr("Bars"), qsTr("Halo"), qsTr("Off")]
                currentIndex: ["ribbon", "bars", "halo", "off"].indexOf(Appearance.visualizer)
                Accessible.name: qsTr("Media visualizer style")
                onActivated: AppearanceStore.setValue("visualizer", ["ribbon", "bars", "halo", "off"][currentIndex])
                background: Rectangle {
                    radius: 6
                    color: Appearance.recess
                    border.width: 1
                    border.color: Appearance.outline
                }
            }
        }
    }
}
