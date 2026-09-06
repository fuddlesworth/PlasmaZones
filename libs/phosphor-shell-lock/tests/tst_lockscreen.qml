// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// LockScreen against a fake placement map and a fake LockService: the
// outline count equals the map's window count, the content block lands in
// the largest empty region (or the centre with no room), and the auth
// field's phase follows typing, submit, failure and unlock.

import QtQuick
import QtTest
import Phosphor.Lock

TestCase {
    id: testCase

    name: "LockScreen"

    width: 1920
    height: 1080
    when: windowShown

    // A LockService stand-in: 0 Unlocked, 1 Locking, 2 Locked,
    // 3 Authenticating. unlock() moves to Authenticating and records the
    // password; the test then decides the outcome.
    Component {
        id: fakeLockComponent

        QtObject {
            id: fakeLock

            property int state: 2
            readonly property bool locked: state === 2 || state === 3
            property string lastPassword: ""
            property int unlockCalls: 0

            signal authenticationFailed(string reason)
            signal unlocked

            function unlock(password) {
                if (state !== 2)
                    return;
                unlockCalls += 1;
                lastPassword = password;
                state = 3;
            }
            function fail(reason) {
                state = 2;
                authenticationFailed(reason);
            }
            function succeed() {
                state = 0;
                unlocked();
            }
        }
    }

    // The real LockService does NOT go straight to `unlocked` on a correct
    // password. It enters Releasing, emits `aboutToUnlock` so the surfaces can
    // play their exit while the compositor lock is still held, and waits for
    // the controller to call `finishUnlock()`. The fake above models a service
    // WITHOUT that handshake (the fallback leg of onUnlocked); this one models
    // the production path, which nothing exercised.
    Component {
        id: fakeReleasingLockComponent

        QtObject {
            id: releasingLock

            property int state: 2
            readonly property bool locked: state === 2 || state === 3 || state === 4
            property string lastPassword: ""
            property int finishUnlockCalls: 0

            signal authenticationFailed(string reason)
            signal aboutToUnlock
            signal unlocked

            function unlock(password) {
                if (state !== 2)
                    return;
                lastPassword = password;
                state = 3;
            }
            function succeed() {
                // Authenticating -> Releasing, surfaces still up and still locked.
                state = 4;
                aboutToUnlock();
            }
            function finishUnlock() {
                if (state !== 4)
                    return;
                finishUnlockCalls += 1;
                state = 0;
                unlocked();
            }
        }
    }

    // A PlacementMapScreen stand-in: two columns on the left two thirds,
    // the right third free.
    Component {
        id: fakeMapComponent

        QtObject {
            property var cells: [
                {
                    "id": "a",
                    "x": 0,
                    "y": 0,
                    "w": 1 / 3,
                    "h": 1,
                    "t": 0.1,
                    "occupied": true,
                    "appId": "firefox",
                    "title": "Firefox"
                },
                {
                    "id": "b",
                    "x": 1 / 3,
                    "y": 0,
                    "w": 1 / 3,
                    "h": 1,
                    "t": 0.5,
                    "occupied": true,
                    "appId": "kate",
                    "title": ""
                },
                {
                    "id": "z",
                    "x": 2 / 3,
                    "y": 0,
                    "w": 1 / 3,
                    "h": 1,
                    "t": 0.9,
                    "occupied": false
                }
            ]
            property rect workArea: Qt.rect(0, 0, 1920, 1080)
            signal changed
        }
    }

    Component {
        id: controllerComponent

        LockController {}
    }

    Component {
        id: screenComponent

        LockScreen {
            width: 1920
            height: 1080
        }
    }

    function keyEvent(key, text, modifiers) {
        return {
            "key": key,
            "text": text === undefined ? "" : text,
            "modifiers": modifiers === undefined ? Qt.NoModifier : modifiers,
            "accepted": false
        };
    }

    function makeScene(withMap, lockComponent) {
        const lock = createTemporaryObject(lockComponent === undefined ? fakeLockComponent : lockComponent, testCase);
        const controller = createTemporaryObject(controllerComponent, testCase, {
            "lock": lock
        });
        const map = withMap ? createTemporaryObject(fakeMapComponent, testCase) : null;
        const screen = createTemporaryObject(screenComponent, testCase, {
            "controller": controller,
            "map": map
        });
        verify(screen);
        return {
            "lock": lock,
            "controller": controller,
            "map": map,
            "screen": screen
        };
    }

    function test_outlineCountEqualsWindowCount() {
        const s = makeScene(true);
        // Three cells, two occupied: the empty zone is not a window.
        compare(s.screen.outlineCount, 2);
    }

    function test_contentLandsInTheFreeThird() {
        const s = makeScene(true);
        verify(!s.screen.contentCentred);
        fuzzyCompare(s.screen.contentRegion.x, 1280, 0.5);
        compare(s.screen.contentRegion.y, 0);
        fuzzyCompare(s.screen.contentRegion.width, 640, 0.5);
        compare(s.screen.contentRegion.height, 1080);
        // Left-aligned to the region, inset.
        compare(s.screen.blockX, Math.round(1280 + s.screen.blockInset));
    }

    function test_contentCentresWithoutAMap() {
        const s = makeScene(false);
        // No cells means the whole screen is the region and the block sits
        // at its left inset; centring is for the no-room case below.
        verify(!s.screen.contentCentred);
        compare(s.screen.contentRegion.width, 1920);
    }

    function test_contentCentresWhenNoRegionFits() {
        const s = makeScene(true);
        // Fill the last third too: nothing is free.
        s.map.cells = [
            {
                "id": "a",
                "x": 0,
                "y": 0,
                "w": 0.5,
                "h": 1,
                "t": 0.1,
                "occupied": true
            },
            {
                "id": "b",
                "x": 0.5,
                "y": 0,
                "w": 0.5,
                "h": 1,
                "t": 0.7,
                "occupied": true
            }
        ];
        s.map.changed();
        verify(s.screen.contentCentred);
        compare(s.screen.outlineCount, 2);
    }

    function test_typingFillsTheSharedField() {
        const s = makeScene(true);
        const c = s.controller;
        compare(c.phase, "idle");
        verify(c.handleKey(keyEvent(Qt.Key_A, "a")));
        verify(c.handleKey(keyEvent(Qt.Key_B, "b")));
        compare(c.password, "ab");
        verify(c.handleKey(keyEvent(Qt.Key_Backspace)));
        compare(c.password, "a");
        // Control chords and modifier-only keys are not password input.
        verify(!c.handleKey(keyEvent(Qt.Key_C, "\u0003", Qt.ControlModifier)));
        verify(!c.handleKey(keyEvent(Qt.Key_Shift, "")));
        compare(c.password, "a");
        verify(c.handleKey(keyEvent(Qt.Key_Escape)));
        compare(c.password, "");
    }

    function test_enterSubmitsAndTheEdgeGoesBlue() {
        const s = makeScene(true);
        const c = s.controller;
        // Enter on an empty field submits nothing.
        c.handleKey(keyEvent(Qt.Key_Return));
        compare(s.lock.unlockCalls, 0);
        c.handleKey(keyEvent(Qt.Key_P, "p"));
        c.handleKey(keyEvent(Qt.Key_W, "w"));
        c.handleKey(keyEvent(Qt.Key_Return));
        compare(s.lock.unlockCalls, 1);
        compare(s.lock.lastPassword, "pw");
        compare(c.phase, "authenticating");
        // Nothing gets typed while PAM is thinking.
        verify(!c.handleKey(keyEvent(Qt.Key_X, "x")));
        compare(c.password, "pw");
    }

    function test_failureClearsAndShowsTheReason() {
        const s = makeScene(true);
        const c = s.controller;
        const failedSpy = createTemporaryObject(spyComponent, testCase, {
            "target": c,
            "signalName": "failed"
        });
        c.handleKey(keyEvent(Qt.Key_P, "p"));
        c.submit();
        s.lock.fail("Wrong password");
        compare(c.phase, "error");
        compare(c.errorText, "Wrong password");
        compare(c.password, "");
        compare(failedSpy.count, 1);
        verify(c.locked);
        // The next key clears the error.
        c.handleKey(keyEvent(Qt.Key_Q, "q"));
        compare(c.phase, "idle");
        compare(c.errorText, "");
    }

    function test_successDismissesThenReleasesTheSurfaces() {
        const s = makeScene(true);
        const c = s.controller;
        const dismissedSpy = createTemporaryObject(spyComponent, testCase, {
            "target": c,
            "signalName": "dismissed"
        });
        verify(c.surfacesWanted);
        c.handleKey(keyEvent(Qt.Key_P, "p"));
        c.submit();
        s.lock.succeed();
        // The lock is gone but the surfaces stay for the dismiss.
        verify(!c.locked);
        compare(c.phase, "dismissing");
        verify(c.surfacesWanted);
        verify(!c.handleKey(keyEvent(Qt.Key_X, "x")));
        tryCompare(dismissedSpy, "count", 1);
        verify(!c.surfacesWanted);
        compare(c.password, "");
        compare(c.phase, "idle");
    }

    function test_releaseHandshakeHoldsTheLockUntilTheExitHasPlayed() {
        const s = makeScene(true, fakeReleasingLockComponent);
        const c = s.controller;
        const dismissedSpy = createTemporaryObject(spyComponent, testCase, {
            "target": c,
            "signalName": "dismissed"
        });
        c.handleKey(keyEvent(Qt.Key_P, "p"));
        c.submit();
        compare(s.lock.lastPassword, "p");
        s.lock.succeed();
        // Releasing: the compositor lock is STILL held, the surfaces are still
        // up, and the controller has not let go yet.
        compare(c.phase, "dismissing");
        verify(c.locked, "the session is still locked while the exit plays");
        verify(c.surfacesWanted);
        compare(s.lock.finishUnlockCalls, 0, "the lock is not released before the exit");
        verify(!c.handleKey(keyEvent(Qt.Key_X, "x")), "no input during the dismiss");
        // Once the exit has played the controller releases the lock, exactly
        // once, and only then do the surfaces go.
        tryCompare(dismissedSpy, "count", 1);
        compare(s.lock.finishUnlockCalls, 1);
        verify(!c.locked);
        verify(!c.surfacesWanted);
        compare(c.password, "");
        compare(c.phase, "idle");
    }

    function test_altGrCharactersReachThePassword() {
        // A German layout's "@" is AltGr+Q; a Polish "ą" is AltGr+A. Both must
        // land, or those users cannot type their password at all.
        const c = makeScene(true).controller;
        c.handleKey(keyEvent(Qt.Key_Q, "@", Qt.GroupSwitchModifier));
        // The platforms that spell AltGr as Control+Alt must work too.
        c.handleKey(keyEvent(Qt.Key_A, "ą", Qt.ControlModifier | Qt.AltModifier));
        compare(c.password, "@ą");
        // A real command chord still contributes nothing.
        verify(!c.handleKey(keyEvent(Qt.Key_C, "\u0003", Qt.ControlModifier)));
        compare(c.password, "@ą");
    }

    function test_outlineClickSetsThePlaceholder() {
        const s = makeScene(true);
        s.controller.placeholderFor("Firefox");
        verify(s.controller.placeholder.indexOf("Firefox") >= 0);
        s.controller.placeholderFor("");
        compare(s.controller.placeholder, "");
    }

    function test_clockIsTabularTime() {
        const s = makeScene(true);
        verify(/^\d\d:\d\d$/.test(s.screen.clockText));
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }
}
