// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-ipc-demo entry QML. Three IpcTargets (greet / count /
// set-value) plus a live event-log panel showing the same events
// `phosphorctl subscribe` would stream, so a single demo window
// covers the entire acceptance walkthrough without the user
// having to open a separate subscribe terminal.

import Phosphor.Ipc
import Phosphor.Theme
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    width: 820
    height: 640
    visible: true
    title: qsTr("Phosphor IPC Demo")
    color: Theme.background

    // Target 1: synchronous greet. `phosphorctl call greet.sayHello --arg name=nate`
    // returns "Hello, nate". No event broadcast, sync only.
    IpcTarget {
        target: "greet"

        function sayHello(name: string): string {
            return "Hello, " + name;
        }
    }

    // Target 2: integer counter with a countChanged signal.
    //
    // State field named `current` rather than `value` to avoid
    // colliding with the QML-auto-generated `valueChanged()`
    // notifier that `property int value` would emit alongside the
    // explicit countChanged(int) and confuse the schema output.
    IpcTarget {
        id: countTarget

        property int current: 0

        target: "count"

        signal countChanged(int v)

        function increment(): int {
            countTarget.current++;
            countTarget.countChanged(countTarget.current);
            countTarget.emitEvent("countChanged", [countTarget.current]);
            demoController.recordEvent("count", "countChanged", [countTarget.current]);
            return countTarget.current;
        }
        function reset(): int {
            countTarget.current = 0;
            countTarget.countChanged(countTarget.current);
            countTarget.emitEvent("countChanged", [countTarget.current]);
            demoController.recordEvent("count", "countChanged", [countTarget.current]);
            return countTarget.current;
        }
    }

    // Target 3: key/value store with a two-arg entryChanged signal.
    // Exercises subscribe payloads with more than one parameter.
    //
    // Signal explicitly named `entryChanged` rather than overloading
    // the QML-auto-generated `storeChanged()` notifier on
    // `property var store`. `storeChanged` only fires when the
    // store REFERENCE is replaced; in-place mutation
    // (`store[key] = value` below) does not fire it. Subscribers
    // therefore wire onto `entryChanged`; `storeChanged` appears in
    // the schema as benign noise but never broadcasts (the QML
    // notifier never fires, AND the broadcast path is explicit
    // `emitEvent` either way).
    IpcTarget {
        id: storeTarget

        property var store: ({})

        target: "set-value"

        signal entryChanged(string key, string value)

        function set(key: string, value: string): bool {
            storeTarget.store[key] = value;
            storeTarget.entryChanged(key, value);
            storeTarget.emitEvent("entryChanged", [key, value]);
            demoController.recordEvent("set-value", "entryChanged", [key, value]);
            return true;
        }
        function get(key: string): string {
            return storeTarget.store[key] !== undefined ? storeTarget.store[key] : "";
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Tokens.spacing_xl
        spacing: Tokens.spacing_l

        Text {
            color: Theme.on_surface
            font.family: Tokens.font_family
            font.pixelSize: Tokens.font_size_display_s
            font.weight: Tokens.font_weight_medium
            text: qsTr("Phosphor.Ipc: call / list / schema / subscribe")
        }

        Text {
            Layout.fillWidth: true
            color: Theme.on_surface_variant
            font.family: Tokens.font_family
            font.pixelSize: Tokens.font_size_body_m
            text: qsTr("Drive the targets from another terminal with phosphorctl. The Live events panel below shows the same events `phosphorctl subscribe` would stream, so no separate subscribe terminal is needed.")
            wrapMode: Text.WordWrap
        }

        // Cheat sheet, promoted above the status / event panels
        // because it's what the user is most likely reaching for
        // when they open the demo. Subscribe is listed first to
        // emphasise the live-events flow.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Tokens.spacing_xxxl * 4
            border.color: Theme.outline_variant
            border.width: 1
            color: Theme.surface_container
            radius: Tokens.radius_m

            Text {
                anchors.fill: parent
                anchors.margins: Tokens.spacing_l
                color: Theme.on_surface
                font.family: "monospace"
                font.pixelSize: Tokens.font_size_body_s
                // When IPC startup failed, demoController.socketPath
                // is empty; show a placeholder instead of emitting a
                // bare `export PHOSPHOR_SOCKET=` line which is
                // a syntax error in the user's shell on paste.
                text: demoController.socketPath.length > 0 ? qsTr("# In another terminal, events from these calls\n# appear in the Live events panel below.\nexport PHOSPHOR_SOCKET=%1\nphosphorctl call count.increment              # one event per call\nphosphorctl call set-value.set --arg k=mood --arg v=happy\nphosphorctl call greet.sayHello --arg name=nate    # sync, no event\nphosphorctl list\nphosphorctl schema count | jq").arg(demoController.socketPath) : qsTr("# IPC startup failed; cheat sheet unavailable until the router is\n# running. See the status footer for the error.")
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignTop
                wrapMode: Text.WordWrap
            }
        }

        // Live event log, newest-first list of broadcast events.
        // The demo records into this list directly from QML, so the
        // panel shows exactly the events any external subscriber
        // would receive over the socket. Lets a single demo window
        // replace the would-be three-terminal acceptance flow.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            border.color: Theme.outline_variant
            border.width: 1
            color: Theme.surface
            radius: Tokens.radius_m

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Tokens.spacing_l
                spacing: Tokens.spacing_s

                Text {
                    color: Theme.on_surface
                    font.family: Tokens.font_family
                    font.pixelSize: Tokens.font_size_body_s
                    font.weight: Tokens.font_weight_medium
                    text: qsTr("Live events (newest first; matches `phosphorctl subscribe` output)")
                }
                ListView {
                    id: eventList

                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    model: demoController.eventLog

                    delegate: Text {
                        required property string modelData

                        color: Theme.on_surface
                        font.family: "monospace"
                        font.pixelSize: Tokens.font_size_body_s
                        text: modelData
                        width: eventList.width
                    }

                    Text {
                        anchors.centerIn: parent
                        color: Theme.on_surface_variant
                        font.family: Tokens.font_family
                        font.pixelSize: Tokens.font_size_body_s
                        text: qsTr("(no events yet: run `phosphorctl call count.increment` from a terminal)")
                        visible: eventList.count === 0
                    }
                }
            }
        }

        // Router status footer (compact). Binds directly to
        // demoController.status. On the success path that string
        // starts with "listening on <socketPath>; ..." so the
        // socket appears once; on the not-started / failed paths
        // it shows "router not started" / "router failed to start
        // (see logs)" respectively. Previous version prefixed an
        // explicit "socket: " line that duplicated the path.
        Text {
            Layout.fillWidth: true
            color: Theme.on_surface_variant
            elide: Text.ElideLeft
            font.family: "monospace"
            font.pixelSize: Tokens.font_size_body_s
            text: demoController.status
        }
    }
}
