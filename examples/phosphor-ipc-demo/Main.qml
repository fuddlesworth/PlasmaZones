// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-ipc-demo entry QML. Three IpcTargets — greet (sync only),
// count (sync + signal), set-value (sync + 2-arg signal) — plus a
// status panel that mirrors the router's registered-targets list
// and the most recent socket path.

import Phosphor.Ipc
import Phosphor.Theme
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    width: 720
    height: 480
    visible: true
    title: qsTr("Phosphor IPC Demo")
    color: Theme.background

    // Target 1: synchronous greet. `phosphorctl call greet.sayHello --arg name=nate`
    // returns "Hello, nate".
    IpcTarget {
        target: "greet"

        function sayHello(name: string): string {
            return "Hello, " + name;
        }
    }

    // Target 2: integer counter with a countChanged signal. The
    // signal is declared so the schema generator can advertise it
    // as subscribable; emitEvent pushes the JSON-shaped event to
    // every subscriber on the wire.
    //
    // The state field is named `current` rather than `value` to
    // avoid colliding with the QML-auto-generated `valueChanged()`
    // notifier — a stray `property int value` would publish a
    // zero-arg `valueChanged` signal alongside the explicit
    // `countChanged(int v)` and confuse the schema output.
    IpcTarget {
        id: countTarget

        property int current: 0

        target: "count"

        signal countChanged(int v)

        function increment(): int {
            countTarget.current++;
            countTarget.countChanged(countTarget.current);
            countTarget.emitEvent("countChanged", [countTarget.current]);
            return countTarget.current;
        }
        function reset(): int {
            countTarget.current = 0;
            countTarget.countChanged(countTarget.current);
            countTarget.emitEvent("countChanged", [countTarget.current]);
            return countTarget.current;
        }
    }

    // Target 3: key/value store with a two-arg entryChanged signal.
    // Exercises subscribe payloads with more than one parameter.
    //
    // The signal is explicitly named `entryChanged` rather than
    // `valueChanged` so it doesn't shadow the QML-auto-generated
    // `storeChanged()` notifier emitted from `property var store`.
    // Subscribers wire onto `entryChanged`; the auto-`storeChanged`
    // shape is benign noise the schema surfaces but no caller
    // subscribes to.
    IpcTarget {
        id: storeTarget

        property var store: ({})

        target: "set-value"

        signal entryChanged(string key, string value)

        function set(key: string, value: string): bool {
            storeTarget.store[key] = value;
            storeTarget.entryChanged(key, value);
            storeTarget.emitEvent("entryChanged", [key, value]);
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
            text: qsTr("Phosphor.Ipc — call / list / schema / subscribe")
        }

        Text {
            Layout.fillWidth: true
            color: Theme.on_surface_variant
            font.family: Tokens.font_family
            font.pixelSize: Tokens.font_size_body_m
            text: qsTr("Drive this demo from another terminal with phosphorctl. The status panel below mirrors the router's registered-targets list.")
            wrapMode: Text.WordWrap
        }

        // Status panel — live router state.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Tokens.spacing_xxxl * 3
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
                    text: qsTr("Socket")
                }
                Text {
                    Layout.fillWidth: true
                    color: Theme.on_surface_variant
                    elide: Text.ElideLeft
                    font.family: "monospace"
                    font.pixelSize: Tokens.font_size_body_s
                    text: demoController.socketPath !== "" ? demoController.socketPath : qsTr("(not running)")
                }
                Text {
                    color: Theme.on_surface
                    font.family: Tokens.font_family
                    font.pixelSize: Tokens.font_size_body_s
                    font.weight: Tokens.font_weight_medium
                    text: qsTr("Status")
                }
                Text {
                    Layout.fillWidth: true
                    color: Theme.on_surface_variant
                    font.family: Tokens.font_family
                    font.pixelSize: Tokens.font_size_body_s
                    text: demoController.status
                    wrapMode: Text.WordWrap
                }
            }
        }

        // Cheat sheet — copies of the commands a user would run.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                text: qsTr("# In another terminal:\nexport PHOSPHOR_SOCKET=") + demoController.socketPath + qsTr("\nphosphorctl list\nphosphorctl schema count | jq\nphosphorctl call greet.sayHello --arg name=nate\nphosphorctl call count.increment\nphosphorctl call set-value.set --arg k=mood --arg v=happy\nphosphorctl subscribe count.countChanged   # streams events on count.increment\nphosphorctl subscribe set-value.entryChanged")
                textFormat: Text.PlainText
                verticalAlignment: Text.AlignTop
                wrapMode: Text.WordWrap
            }
        }
    }
}
