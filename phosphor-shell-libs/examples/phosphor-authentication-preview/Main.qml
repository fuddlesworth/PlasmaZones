// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import Phosphor.Ipc
import Phosphor.Polkit
import Phosphor.Theme

Window {
    id: window

    property string initialExample: "file"
    property string initialState: "ready"
    property bool windowed: false

    width: 1280
    height: 800
    visibility: fixture.presented ? (windowed ? Window.Windowed : Window.FullScreen) : Window.Hidden
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    title: qsTr("Phosphor authentication preview")

    Component.onCompleted: {
        if (!fixture.show(initialExample, initialState)) {
            console.warn("Unknown authentication preview example or state.");
            Qt.quit();
        }
    }

    function findNamedItem(item: Item, name: string): Item {
        if (item.objectName === name)
            return item;
        for (let index = 0; index < item.children.length; ++index) {
            const match = findNamedItem(item.children[index], name);
            if (match)
                return match;
        }
        return null;
    }

    function itemMetrics(name: string): var {
        const item = findNamedItem(window.contentItem, name);
        if (!item)
            return null;
        const rect = item.mapToItem(window.contentItem, 0, 0, item.width, item.height);
        return {
            x: rect.x,
            y: rect.y,
            w: rect.width,
            h: rect.height,
            visible: window.visible && item.visible,
            enabled: item.enabled
        };
    }

    AuthenticationFixture {
        id: fixture
    }

    QtObject {
        id: keyboardState
        property bool capsLock: false
        property string layoutName: "English (US)"
        readonly property bool canCycle: true

        function nextLayout(): void {
            layoutName = layoutName === "English (US)" ? "German" : "English (US)";
        }
    }

    PolkitSurface {
        id: surface
        anchors.fill: parent
        agent: fixture
        request: fixture.displayRequest
        requester: fixture.requesterName
        requesterProgram: fixture.requesterProgram
        resourceText: fixture.requesterResource
        errorText: fixture.lastError
        keyboard: keyboardState
    }

    Connections {
        target: surface.prompt

        function onCancelled(): void {
            fixture.presented = false;
        }
    }

    IpcTarget {
        target: "preview"

        function show(example: string, state: string): bool {
            return fixture.show(example, state);
        }

        function phase(name: string): bool {
            return fixture.setState(name);
        }

        function result(name: string): bool {
            if (name !== "success" && name !== "error" && name !== "unavailable")
                return false;
            return fixture.setState(name);
        }

        function keyboard(caps: bool, layout: string): void {
            keyboardState.capsLock = caps;
            keyboardState.layoutName = layout;
        }

        function preset(name: string): bool {
            return AppearanceStore.applyPreset(name);
        }

        function motion(enabled: bool): bool {
            return AppearanceStore.setValue("motion", enabled);
        }

        function textScale(percent: int): bool {
            return AppearanceStore.setValue("textScale", percent);
        }

        function state(): string {
            // No field text, response, password length, or cookie is exported.
            const controls = {};
            for (const name of ["polkitField", "polkitReveal", "polkitDetails", "polkitCancel", "polkitSubmit", "polkitIdentity"])
                controls[name] = window.itemMetrics(name);
            return JSON.stringify({
                example: fixture.example,
                phase: fixture.phase,
                visible: window.visible,
                active: fixture.activeRequest !== null,
                inputReady: fixture.inputReady,
                attempts: fixture.attempts,
                submissions: fixture.submissions,
                cancellations: fixture.cancellations,
                identityChanges: fixture.identityChanges,
                selectedIdentity: fixture.displayRequest ? fixture.displayRequest.selectedIdentity : -1,
                echo: fixture.displayRequest ? fixture.displayRequest.echo : false,
                capsLock: keyboardState.capsLock,
                layout: keyboardState.layoutName,
                width: window.width,
                height: window.height,
                activeFocusItem: window.activeFocusItem ? window.activeFocusItem.objectName : "",
                card: window.itemMetrics("polkitPrompt"),
                controls: controls,
                events: fixture.events
            });
        }

        function quit(): void {
            fixture.cancel();
            Qt.quit();
        }
    }
}
