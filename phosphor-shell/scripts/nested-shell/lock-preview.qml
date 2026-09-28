// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// UI fixture only. No authentication or system power action is performed.
import QtQuick
import Phosphor.Shell
import Phosphor.Lock
import Phosphor.Service.Lock
import Phosphor.Theme
import Phosphor.Ipc

Item {
    id: root
    property bool mediaAvailable: true
    property int notices: 3
    // Query support and identity without requesting a compositor lock.
    LockService {
        id: realService
    }
    LockKeyboard {
        id: keyboardState
    }
    QtObject {
        id: fixtureLock
        property int state: 2
        readonly property bool locked: state >= 2
        signal authenticationFailed(string reason)
        signal aboutToUnlock
        signal unlocked
        function unlock(password) {
            state = 3;
        }
        function finishUnlock() {
            state = 0;
            unlocked();
        }
    }
    LockController {
        id: authController
        lock: fixtureLock
    }
    QtObject {
        id: fixtureSession
        property int canSuspend: 1
        property int canReboot: 1
        property int canPowerOff: 1
        property string lastAction: ""
        function refreshCapabilities() {
        }
        function suspend() {
            lastAction = "suspend";
        }
        function reboot() {
            lastAction = "reboot";
        }
        function powerOff() {
            lastAction = "powerOff";
        }
    }
    QtObject {
        id: fixturePlayer
        property string identity: "Preview player"
        property string trackTitle: "A quiet place"
        property string trackArtist: "Phosphor"
        property string trackArtUrl: ""
        property bool isPlaying: true
        property bool canControl: true
        property bool canGoPrevious: true
        property bool canGoNext: true
        property bool canPlay: true
        property bool canPause: true
        function next() {
            trackTitle = "The next morning";
        }
        function previous() {
            trackTitle = "A quiet place";
        }
        function togglePlaying() {
            isPlaying = !isPlaying;
        }
    }
    QtObject {
        id: fixtureSpectrum
        property var samples: [.1, .2, .4, .3, .55, .8, .7, .35, .65, .95, .6, .2, .4, .25, .12, .08]
        function setActive(owner, active) {
        }
    }
    PerScreenPanels {
        model: PhosphorShell.screens
        delegate: PanelWindow {
            id: panel
            Connections {
                target: authController
                function onDismissed() {
                    if (panel.Window.window)
                        panel.Window.window.visible = false;
                }
                function onSurfacesWantedChanged() {
                    if (authController.surfacesWanted && panel.Window.window)
                        panel.Window.window.visible = true;
                }
            }
            edge: PanelWindow.Top
            alignment: PanelWindow.Fill
            thickness: modelData.height
            panelLayer: PanelWindow.LayerOverlay
            exclusiveZoneEnabled: false
            keyboardFocus: PanelWindow.Exclusive
            LockScreen {
                anchors.fill: parent
                controller: authController
                keyboard: keyboardState
                session: fixtureSession
                player: root.mediaAvailable ? fixturePlayer : null
                spectrum: fixtureSpectrum
                userName: realService.userName
                notificationCount: root.notices
            }
        }
    }
    IpcTarget {
        target: "preview"
        function preset(name: string): bool {
            return AppearanceStore.applyPreset(name);
        }
        function layout(name: string): bool {
            return AppearanceStore.setValue("lockLayout", name);
        }
        function media(enabled: bool): bool {
            return AppearanceStore.setValue("lockMedia", enabled);
        }
        function notifications(enabled: bool): bool {
            return AppearanceStore.setValue("lockNotifications", enabled);
        }
        function visualizer(name: string): bool {
            return AppearanceStore.setValue("visualizer", name);
        }
        function motion(enabled: bool): bool {
            return AppearanceStore.setValue("motion", enabled);
        }
        function state(): string {
            // Never expose entered credentials through IPC, logs or captures.
            return JSON.stringify({
                phase: authController.phase,
                lastAction: fixtureSession.lastAction,
                layout: keyboardState.layoutName,
                capsLock: keyboardState.capsLock,
                protocolSupported: realService.supported,
                playing: fixturePlayer.isPlaying
            });
        }
        function result(name: string): void {
            if (name === "error") {
                fixtureLock.state = 2;
                fixtureLock.authenticationFailed(qsTr("That password didn’t match. Try again."));
            } else if (name === "success") {
                fixtureLock.state = 4;
                fixtureLock.aboutToUnlock();
            } else {
                fixtureLock.state = 2;
                authController.clear();
            }
        }
    }
}
