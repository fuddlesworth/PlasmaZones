// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-osd-demo, the Phase 3.3 acceptance demo.
//
// An OSDHost overlay driven two ways: in-window buttons, and
// `phosphorctl call osd.show <kind> <value>` over IPC. The four built-in
// OSDs are supplied by OSDController's Registry<IOSDFactory>. Repeated
// triggers of the same kind refresh one OSD (debounce/dedupe) and restart
// its hold timer.

import Phosphor.Ipc
import Phosphor.OSD
import Phosphor.Theme
import Phosphor.Widgets
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    // Local state so the buttons produce changing values / toggles.
    property int volume: 40
    property int brightness: 60
    property bool micMuted: false
    property bool capsOn: false

    width: 720
    height: 560
    visible: true
    title: qsTr("Phosphor OSD Demo")
    color: Theme.background

    // IPC: `phosphorctl call osd.show --arg kind=volume --arg value=62`.
    // A value of 0 reads as the "off/muted" state for the stateful OSDs.
    IpcTarget {
        target: "osd"

        function show(kind: string, value: int): bool {
            osdHost.show(kind, value, value !== 0, "");
            return true;
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        Label {
            text: qsTr("On-Screen Displays")
            color: Theme.on_surface
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Trigger an OSD with a button or from a terminal. Repeated triggers of the same OSD refresh it and restart the timer.")
            color: Theme.on_surface_variant
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        Flow {
            Layout.fillWidth: true
            spacing: 12

            PhosphorButton {
                text: qsTr("Volume −")
                variant: PhosphorButton.Tonal
                onClicked: {
                    root.volume = Math.max(0, root.volume - 15);
                    osdHost.show("volume", root.volume, undefined, "");
                }
            }

            PhosphorButton {
                text: qsTr("Volume +")
                variant: PhosphorButton.Tonal
                onClicked: {
                    root.volume = Math.min(100, root.volume + 15);
                    osdHost.show("volume", root.volume, undefined, "");
                }
            }

            PhosphorButton {
                text: qsTr("Brightness +")
                variant: PhosphorButton.Tonal
                onClicked: {
                    root.brightness = root.brightness >= 100 ? 20 : Math.min(100, root.brightness + 20);
                    osdHost.show("brightness", root.brightness, undefined, "");
                }
            }

            PhosphorButton {
                text: qsTr("Mic")
                variant: PhosphorButton.Outlined
                onClicked: {
                    root.micMuted = !root.micMuted;
                    osdHost.show("mic", undefined, root.micMuted, "");
                }
            }

            PhosphorButton {
                text: qsTr("Caps Lock")
                variant: PhosphorButton.Outlined
                onClicked: {
                    root.capsOn = !root.capsOn;
                    osdHost.show("caps", undefined, root.capsOn, "");
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }

        // phosphorctl cheat sheet.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            radius: 12
            color: Theme.surface_container
            border.width: 1
            border.color: Theme.outline_variant

            Text {
                anchors.fill: parent
                anchors.margins: 14
                color: Theme.on_surface
                font.family: "monospace"
                font.pixelSize: 12
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                text: ipcSocketPath.length > 0 ? qsTr("export PHOSPHOR_SOCKET=%1\nphosphorctl call osd.show --arg kind=volume --arg value=62\nphosphorctl call osd.show --arg kind=mic --arg value=1\nphosphorctl schema osd | jq").arg(ipcSocketPath) : qsTr("# IPC router not running; use the buttons above.")
            }
        }
    }

    // OSD overlay on top of everything. One host here; production wraps
    // OSDHost in PerScreen for one per monitor.
    OSDHost {
        id: osdHost

        anchors.fill: parent
        // Float clear of the cheat sheet at the very bottom of this demo
        // window. A real shell uses the default (near the bottom edge).
        bottomMargin: 160
        provider: osdController
    }
}
