// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Lock
import Phosphor.Theme

TestCase {
    id: testCase
    name: "LockControls"
    width: 1440
    height: 900
    visible: true
    when: windowShown
    Component {
        id: sessionComponent
        QtObject {
            property int canSuspend: 1
            property int canReboot: 1
            property int canPowerOff: 1
            property int sleeps: 0
            property int reboots: 0
            property int shutdowns: 0
            function refreshCapabilities() {
            }
            function suspend() {
                ++sleeps;
            }
            function reboot() {
                ++reboots;
            }
            function powerOff() {
                ++shutdowns;
            }
        }
    }
    Component {
        id: powerComponent
        LockPowerMenu {
            x: 1200
            y: 820
        }
    }
    Component {
        id: screenComponent
        LockScreen {
            width: 1440
            height: 900
        }
    }
    Component {
        id: playerComponent
        QtObject {
            property string identity: "Test player"
            property string trackTitle: "Private song"
            property string trackArtist: "Private artist"
            property string trackArtUrl: ""
            property bool isPlaying: true
            property bool canControl: true
            property bool canGoPrevious: false
            property bool canGoNext: true
            property bool canPlay: true
            property bool canPause: true
            property int nextCalls: 0
            property int previousCalls: 0
            property int toggleCalls: 0
            function next() {
                ++nextCalls;
            }
            function previous() {
                ++previousCalls;
            }
            function togglePlaying() {
                ++toggleCalls;
                isPlaying = !isPlaying;
            }
        }
    }
    function init() {
        AppearanceStore.setValue("lockLayout", "split");
        AppearanceStore.setValue("lockMedia", false);
        AppearanceStore.setValue("lockNotifications", true);
        AppearanceStore.setValue("motion", false);
    }
    function test_powerRequiresExplicitConfirmationAndRechecksCapability() {
        const session = createTemporaryObject(sessionComponent, testCase);
        const power = createTemporaryObject(powerComponent, testCase, {
            session: session
        });
        mouseClick(findChild(power, "lockPowerButton"));
        verify(power.open);
        mouseClick(findChild(power, "lockAction-reboot"));
        compare(session.reboots, 0);
        mouseClick(findChild(power, "lockPowerCancel"));
        verify(!power.open);
        compare(session.reboots, 0);
        mouseClick(findChild(power, "lockPowerButton"));
        mouseClick(findChild(power, "lockAction-reboot"));
        session.canReboot = 2;
        mouseClick(findChild(power, "lockPowerConfirm"));
        compare(session.reboots, 0);
        session.canReboot = 1;
        mouseClick(findChild(power, "lockPowerConfirm"));
        compare(session.reboots, 1);
        verify(!power.open);
        mouseClick(findChild(power, "lockPowerButton"));
        mouseClick(findChild(power, "lockAction-suspend"));
        compare(session.sleeps, 1);
        compare(session.shutdowns, 0);
        verify(!power.open);
    }
    function test_lockedMenuOmitsActionsNeedingExternalAuthorization() {
        const session = createTemporaryObject(sessionComponent, testCase, {
            canSuspend: 4,
            canReboot: 0,
            canPowerOff: 3
        });
        const power = createTemporaryObject(powerComponent, testCase, {
            session: session
        });
        mouseClick(findChild(power, "lockPowerButton"));
        verify(!findChild(power, "lockAction-suspend").visible);
        verify(!findChild(power, "lockAction-reboot").visible);
        verify(!findChild(power, "lockAction-powerOff").visible);
        compare(session.sleeps, 0);
    }
    function test_mediaIsOptInAndTransportRespectsCapabilities() {
        const player = createTemporaryObject(playerComponent, testCase);
        const screen = createTemporaryObject(screenComponent, testCase, {
            player: player,
            notificationCount: 3
        });
        compare(findChild(screen, "lockMedia"), null);
        verify(findChild(screen, "lockNotificationCount").visible);
        AppearanceStore.setValue("lockMedia", true);
        tryVerify(() => findChild(screen, "lockMedia") !== null);
        waitForPolish(screen);
        waitForRendering(screen);
        verify(!findChild(screen, "lockPrevious").enabled);
        mouseClick(findChild(screen, "lockNext"));
        compare(player.nextCalls, 1);
        mouseClick(findChild(screen, "lockPlay"));
        compare(player.toggleCalls, 1);
        compare(player.isPlaying, false);
        compare(player.previousCalls, 0);
        AppearanceStore.setValue("lockMedia", false);
        tryCompare(screen, "showMedia", false);
        tryVerify(() => findChild(screen, "lockMedia") === null);
        AppearanceStore.setValue("lockNotifications", false);
        verify(!findChild(screen, "lockNotificationCount").visible);
    }
}
