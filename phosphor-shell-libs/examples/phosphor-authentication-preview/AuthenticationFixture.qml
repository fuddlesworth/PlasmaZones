// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Preview-only agent. Credentials are compared locally with demo strings,
// discarded immediately, and never stored in the event log or sent over IPC.

import QtQuick

Item {
    id: fixture

    property var activeRequest: null
    property var displayRequest: null
    property string requesterName: "Kate"
    readonly property string requesterProgram: displayRequest ? displayRequest.details.program || "" : ""
    property string requesterResource: ""
    property string phase: "idle"
    property string lastError: ""
    property string info: ""
    readonly property bool inputReady: phase === "prompt"
    readonly property bool busy: phase === "starting" || phase === "checking"
    readonly property bool canRetry: phase === "unavailable" && attempts < 3
    property bool presented: false
    property string example: "file"
    property int attempts: 0
    property int submissions: 0
    property int cancellations: 0
    property int identityChanges: 0
    property var events: []
    property bool pendingAccepted: false

    signal promptRequested(string prompt, bool echo)
    signal authenticationCompleted(bool gained)

    function record(event: string): void {
        events = events.concat([event]).slice(-32);
    }

    function stopPending(): void {
        replyTimer.stop();
        dismissTimer.stop();
        pendingAccepted = false;
    }

    function show(name: string, state: string): bool {
        const examples = {
            file: {
                requester: "Kate",
                iconName: "kate",
                message: qsTr("Authentication is required to save the file /etc/hosts."),
                actionId: "org.kde.ktexteditor.katetextbuffer.savefile",
                program: "/usr/bin/kate",
                resource: "/etc/hosts"
            },
            software: {
                requester: "Discover",
                iconName: "plasmadiscover",
                message: qsTr("Authentication is required to install software for all users."),
                actionId: "org.freedesktop.packagekit.package-install",
                program: "/usr/bin/plasma-discover"
            },
            accounts: {
                requester: "System Settings",
                iconName: "preferences-system",
                message: qsTr("Authentication is required to change the system time."),
                actionId: "org.freedesktop.timedate1.set-time",
                program: "/usr/bin/systemsettings",
                identities: ["nlavender", "root"]
            },
            code: {
                requester: "Kate",
                iconName: "kate",
                message: qsTr("Enter your verification code to finish authenticating this request."),
                actionId: "org.kde.ktexteditor.katetextbuffer.savefile",
                program: "/usr/bin/kate",
                prompt: qsTr("Verification code:"),
                echo: true
            },
            background: {
                requester: "",
                iconName: "security-high",
                message: qsTr("Authentication is required to reload the system configuration."),
                actionId: "org.freedesktop.systemd1.reload-daemon",
                program: ""
            },
            long: {
                requester: "System Settings",
                iconName: "network-wired",
                message: qsTr("Administrator permission is required to change the connection used by everyone on this computer. This includes the network address, routing, and DNS settings for the shared Ethernet connection. Existing connections may be interrupted while these settings are applied. Review the connection settings before continuing."),
                actionId: "org.freedesktop.NetworkManager.settings.modify.system",
                program: "/usr/bin/systemsettings",
                identities: ["nlavender", "Network administrator", "root"]
            }
        };
        if (!examples[name] || !validState(state))
            return false;
        stopPending();
        const oldRequest = displayRequest;
        const data = examples[name];
        activeRequest = null;
        example = name;
        requesterName = data.requester;
        requesterResource = data.resource || "";
        attempts = 0;
        lastError = "";
        info = "";
        displayRequest = requestFactory.createObject(fixture, {
            message: data.message,
            actionId: data.actionId,
            iconName: data.iconName,
            details: data.program ? {
                "program": data.program
            } : {},
            identities: data.identities || ["nlavender"],
            prompt: data.prompt || qsTr("Password:"),
            echo: data.echo || false
        });
        activeRequest = displayRequest;
        if (oldRequest)
            oldRequest.destroy();
        presented = true;
        record("request:" + name);
        return setState(state);
    }

    function validState(name: string): bool {
        return ["ready", "prompt", "starting", "error", "checking", "success", "cancel", "cancelled", "unavailable", "failed"].indexOf(name) >= 0;
    }

    function setState(name: string): bool {
        if (!displayRequest || !validState(name))
            return false;
        stopPending();
        lastError = "";
        info = "";
        activeRequest = displayRequest;
        presented = true;
        if (name === "cancel" || name === "cancelled") {
            cancel();
        } else if (name === "success" || name === "failed") {
            finish(name === "success", true);
        } else if (name === "unavailable") {
            phase = "unavailable";
            lastError = qsTr("Authentication is unavailable. Try again or cancel this request.");
        } else if (name === "checking" || name === "starting") {
            phase = name;
        } else {
            if (name === "error")
                lastError = displayRequest.echo ? qsTr("That code didn’t match. Try again.") : qsTr("That password didn’t match. Try again.");
            phase = "prompt";
            promptRequested(displayRequest.prompt, displayRequest.echo);
        }
        record("state:" + phase);
        return true;
    }

    function respond(response: string): void {
        if (!activeRequest || !inputReady || !response.length)
            return;
        // Keep only the comparison result while the simulated exchange runs.
        pendingAccepted = response === (activeRequest.echo ? "123456" : "demo");
        response = "";
        submissions++;
        phase = "checking";
        lastError = "";
        record("respond");
        replyTimer.restart();
    }

    function clearError(): void {
        lastError = "";
    }

    function completeResponse(): void {
        if (!activeRequest || phase !== "checking")
            return;
        const accepted = pendingAccepted;
        pendingAccepted = false;
        attempts++;
        if (accepted || attempts >= 3) {
            finish(accepted, false);
            return;
        }
        lastError = activeRequest.echo ? qsTr("That code didn’t match. Try again.") : qsTr("That password didn’t match. Try again.");
        phase = "prompt";
        promptRequested(activeRequest.prompt, activeRequest.echo);
        record("retry-prompt");
    }

    function finish(gained: bool, hold: bool): void {
        stopPending();
        phase = gained ? "success" : "failed";
        if (!gained)
            lastError = qsTr("Authentication failed. Start the action again to retry.");
        activeRequest = null;
        record(phase);
        authenticationCompleted(gained);
        if (!hold)
            dismissTimer.restart();
    }

    function cancel(): void {
        stopPending();
        cancellations++;
        phase = "cancelled";
        activeRequest = null;
        presented = false;
        record("cancel");
        authenticationCompleted(false);
    }

    function retry(): void {
        if (!activeRequest || !canRetry)
            return;
        setState("ready");
        record("retry");
    }

    function selectIdentity(index: int): void {
        if (!activeRequest || index < 0 || index >= activeRequest.identities.length || index === activeRequest.selectedIdentity)
            return;
        stopPending();
        activeRequest.selectedIdentity = index;
        identityChanges++;
        lastError = "";
        phase = "prompt";
        promptRequested(activeRequest.prompt, activeRequest.echo);
        record("identity:" + index);
    }

    Component {
        id: requestFactory

        QtObject {
            property string actionId: ""
            property string message: ""
            property string iconName: ""
            property var details: ({})
            property var identities: []
            property int selectedIdentity: 0
            property string prompt: ""
            property bool echo: false
        }
    }

    Timer {
        id: replyTimer
        interval: 900
        onTriggered: fixture.completeResponse()
    }

    Timer {
        id: dismissTimer
        interval: 450
        onTriggered: fixture.presented = false
    }
}
