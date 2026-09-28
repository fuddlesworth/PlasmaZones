// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    objectName: "polkitPrompt"
    property var agent: null
    // Capture this request when opening. A closing card must never act on its successor.
    property var request: null
    property var keyboard: null
    property string requester: ""
    property string requesterProgram: agent && agent.requesterProgram !== undefined ? agent.requesterProgram : ""
    property string resourceText: agent && agent.requesterResource !== undefined ? agent.requesterResource : ""
    property string errorText: agent ? agent.lastError : ""
    property Component decoration: null
    property alias password: passwordField.text
    property bool detailsOpen: false
    property bool errorDismissed: false
    property bool submissionPending: false
    readonly property bool current: !!request && !!agent && agent.activeRequest === request
    readonly property string phase: agent ? agent.phase : "idle"
    readonly property bool inputReady: current && agent.inputReady && !submissionPending
    readonly property bool busy: current && agent.busy
    readonly property bool canRetry: current && agent.canRetry
    readonly property bool success: phase === "success"
    readonly property bool hasError: errorText !== "" && !errorDismissed
    readonly property real contentPadding: Appearance.padding + 6
    readonly property color errorColor: Appearance.light ? "#a32742" : "#fda2a4"
    readonly property color successColor: Appearance.light ? "#256b50" : "#89cfb1"
    readonly property string message: request ? request.message : ""
    readonly property string actionId: request ? request.actionId : ""
    readonly property string fieldPrompt: {
        const label = request ? String(request.prompt).trim().replace(/:$/, "") : "";
        return label || qsTr("Password");
    }
    readonly property bool echo: !!request && request.echo
    readonly property var identities: request ? request.identities : []
    readonly property int selectedIdentity: request ? request.selectedIdentity : 0
    readonly property string identityName: identities.length > selectedIdentity ? String(identities[selectedIdentity]).replace(/^unix-user:/, "") : qsTr("Administrator")
    readonly property Item firstControl: identityChoice.visible && identityChoice.enabled ? identityChoice : passwordField.enabled ? passwordField.field : detailsButton
    readonly property string statusText: {
        if (success)
            return qsTr("Authenticated. Returning to your app.");
        if (hasError)
            return errorText;
        if (agent && agent.info !== "")
            return agent.info;
        if (phase === "starting")
            return qsTr("Preparing authentication…");
        if (phase === "checking")
            return qsTr("Checking your credentials…");
        if (phase === "unavailable")
            return qsTr("Authentication is unavailable. Try again or cancel this request.");
        if (phase === "failed")
            return qsTr("Authentication failed.");
        return qsTr("Authenticate to continue.");
    }
    implicitWidth: 460
    implicitHeight: header.height + content.implicitHeight + footer.implicitHeight
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Authentication required")
    Accessible.description: message
    signal submitted
    signal cancelled

    function focusInput(): void {
        if (!current)
            return;
        if (inputReady)
            passwordField.field.forceActiveFocus();
        else
            cancelButton.forceActiveFocus();
    }
    function resetInput(): void {
        if (passwordField)
            passwordField.clear();
        submissionPending = false;
        errorDismissed = false;
    }
    function submit(): void {
        if (!inputReady || passwordField.text.length === 0)
            return;
        const response = passwordField.text;
        passwordField.clear();
        submissionPending = true;
        agent.respond(response);
        submitted();
        focusInput();
    }
    function cancel(): void {
        passwordField.clear();
        if (current)
            agent.cancel();
        cancelled();
    }
    onRequestChanged: {
        resetInput();
        detailsOpen = false;
    }
    onErrorTextChanged: if (errorText !== "") {
        passwordField.clear();
        errorDismissed = false;
    }
    onInputReadyChanged: if (inputReady)
        Qt.callLater(focusInput)
    Keys.onEscapePressed: cancel()
    Component.onCompleted: Qt.callLater(focusInput)
    Component.onDestruction: passwordField.clear()
    Connections {
        target: root.agent
        function onPromptRequested(prompt: string, echo: bool): void {
            if (!root.current)
                return;
            root.resetInput();
            Qt.callLater(root.focusInput);
        }
    }
    Connections {
        target: root.request
        function onSelectedIdentityChanged(): void {
            root.resetInput();
        }
    }
    component AuthText: Text {
        property real size: 12
        color: Appearance.text
        font.family: Tokens.font_family_ui
        font.pixelSize: Math.round(size * Appearance.textScale)
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }
    ShellSurface {
        id: ground
        property bool shaderAnchor: true
        anchors.fill: parent
        accented: true
    }
    DecorationSlot {
        anchors.fill: parent
        component: root.decoration
        contentItem: ground
        surfacePath: "shell.phosphor.popout"
        layeredStages: true
    }
    Item {
        id: header
        width: parent.width
        height: Math.max(Appearance.compact ? 50 : 56, branding.implicitHeight + 22)
        RowLayout {
            id: branding
            anchors.fill: parent
            anchors.leftMargin: root.contentPadding - 7
            anchors.rightMargin: root.contentPadding
            spacing: 10
            PhosphorMark {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                color: Appearance.stops[0]
            }
            AuthText {
                text: qsTr("Authentication").toUpperCase()
                size: 10
                color: Appearance.muted
                font.letterSpacing: 1.5
                Layout.fillWidth: true
            }
            ShellIcon {
                source: "security-high"
                color: root.success ? root.successColor : Appearance.muted
                Layout.preferredWidth: 17
                Layout.preferredHeight: 17
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Appearance.outline
        }
    }
    Flickable {
        id: content
        objectName: "polkitContent"
        anchors.top: header.bottom
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        implicitHeight: body.implicitHeight + topMargin + bottomMargin
        contentWidth: width
        contentHeight: body.implicitHeight
        topMargin: Appearance.compact ? 16 : 20
        bottomMargin: Appearance.compact ? 12 : 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        function revealFocus(): void {
            const item = root.Window.window ? root.Window.window.activeFocusItem : null;
            if (!item || !root.activeFocus)
                return;
            let ancestor = item;
            while (ancestor && ancestor !== content.contentItem)
                ancestor = ancestor.parent;
            if (!ancestor)
                return;
            const top = item.mapToItem(content.contentItem, 0, 0).y;
            const bottom = top + item.height;
            if (bottom > contentY + height - bottomMargin)
                contentY = bottom - height + bottomMargin;
            if (top < contentY + topMargin)
                contentY = top - topMargin;
        }
        onHeightChanged: Qt.callLater(revealFocus)
        Connections {
            target: root.Window.window
            function onActiveFocusItemChanged(): void {
                Qt.callLater(content.revealFocus);
            }
        }
        ColumnLayout {
            id: body
            x: root.contentPadding
            width: Math.max(0, content.width - 2 * root.contentPadding)
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                spacing: 11
                Rectangle {
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    radius: Appearance.radius * .5
                    color: Qt.alpha(Appearance.card, .6)
                    border.color: Appearance.outline
                    ShellIcon {
                        anchors.centerIn: parent
                        width: 20
                        height: 20
                        isMask: false
                        // Icon names from policies are theme identifiers, never remote URLs.
                        source: root.request && /^[a-zA-Z0-9._-]+$/.test(root.request.iconName) ? root.request.iconName : "security-high"
                        color: Appearance.accent
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    AuthText {
                        objectName: "polkitRequester"
                        text: root.requester || qsTr("System service")
                        size: 16
                        font.weight: Font.Medium
                        Layout.fillWidth: true
                    }
                    AuthText {
                        text: root.requester ? qsTr("Requesting permission") : qsTr("Background request")
                        color: Appearance.muted
                        Layout.fillWidth: true
                    }
                }
            }
            AuthText {
                text: qsTr("Authentication required")
                size: 24
                font.weight: Font.Medium
                font.letterSpacing: -.7
                Layout.topMargin: 18
                Layout.fillWidth: true
            }
            AuthText {
                objectName: "polkitDescription"
                text: root.message
                size: 16
                lineHeightMode: Text.FixedHeight
                lineHeight: Math.round(24 * Appearance.textScale)
                Layout.topMargin: 8
                Layout.fillWidth: true
            }
            Rectangle {
                visible: root.resourceText !== ""
                Layout.fillWidth: true
                Layout.topMargin: visible ? 12 : 0
                implicitHeight: resourceLabel.implicitHeight + 16
                color: Qt.alpha(Appearance.recess, .6)
                radius: 5
                Rectangle {
                    width: 2
                    height: parent.height
                    color: Qt.alpha(Appearance.stops[2], .6)
                }
                AuthText {
                    id: resourceLabel
                    anchors.fill: parent
                    anchors.margins: 8
                    anchors.leftMargin: 10
                    text: root.resourceText
                    font.family: Tokens.font_family_mono
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Appearance.compact ? 16 : 20
                implicitHeight: 1
                color: Appearance.outline
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Appearance.compact ? 12 : 16
                spacing: 11
                Rectangle {
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    radius: Appearance.radius * .55
                    color: Qt.tint(Appearance.recess, Qt.alpha(Appearance.stops[2], .13))
                    border.color: Qt.alpha(Appearance.stops[2], .4)
                    AuthText {
                        anchors.centerIn: parent
                        text: root.identityName.slice(0, 1)
                        size: 16
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    AuthText {
                        text: qsTr("Authenticate as")
                        color: Appearance.muted
                        Layout.fillWidth: true
                    }
                    AuthText {
                        text: root.identityName
                        size: 16
                        visible: root.identities.length <= 1
                        Layout.fillWidth: true
                    }
                    ShellComboBox {
                        id: identityChoice
                        objectName: "polkitIdentity"
                        Layout.fillWidth: true
                        Layout.topMargin: 5
                        visible: root.identities.length > 1
                        enabled: root.current && !root.busy && !root.success
                        model: root.identities.map(identity => String(identity).replace(/^unix-user:/, ""))
                        currentIndex: root.selectedIdentity
                        displayText: root.identityName
                        implicitHeight: 36
                        labelSize: 14
                        Accessible.name: qsTr("Authenticate as")
                        onActivated: index => {
                            if (!root.current)
                                return;
                            root.resetInput();
                            root.agent.selectIdentity(index);
                        }
                        KeyNavigation.tab: passwordField.field
                        KeyNavigation.backtab: submitButton.enabled ? submitButton : cancelButton
                        indicator: ShellIcon {
                            source: "go-down"
                            color: Appearance.muted
                            width: 14
                            height: 14
                            x: identityChoice.width - width - 10
                            y: (identityChoice.height - height) / 2
                        }
                        background: Rectangle {
                            implicitHeight: 36
                            radius: Appearance.radius * .4
                            color: Appearance.recess
                            border.color: identityChoice.visualFocus ? Appearance.text : Appearance.outline
                        }
                    }
                }
            }
            AuthText {
                objectName: "polkitFieldLabel"
                text: root.fieldPrompt
                color: Appearance.muted
                Layout.topMargin: 16
                Layout.fillWidth: true
            }
            PolkitPasswordField {
                id: passwordField
                Layout.fillWidth: true
                Layout.topMargin: 8
                enabled: root.inputReady
                echo: root.echo
                label: root.fieldPrompt
                invalid: root.hasError
                nextFocusItem: detailsButton
                previousFocusItem: identityChoice.visible && identityChoice.enabled ? identityChoice : submitButton.enabled ? submitButton : cancelButton
                onAccepted: root.submit()
                onEdited: root.errorDismissed = true
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumHeight: 28
                spacing: 6
                ShellIcon {
                    source: "input-keyboard"
                    color: Appearance.muted
                    Layout.preferredWidth: 13
                    Layout.preferredHeight: 13
                    visible: !!root.keyboard && root.keyboard.layoutName !== ""
                }
                AuthText {
                    objectName: "polkitKeyboard"
                    text: root.keyboard ? root.keyboard.layoutName : ""
                    color: Appearance.muted
                    Layout.fillWidth: true
                }
                AuthText {
                    objectName: "polkitCaps"
                    text: qsTr("Caps Lock is on")
                    color: Appearance.stops[3]
                    visible: !!root.keyboard && root.keyboard.capsLock
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumHeight: Math.round(44 * Appearance.textScale)
                Layout.topMargin: 4
                spacing: 8
                BusyIndicator {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    Layout.alignment: Qt.AlignTop
                    visible: root.busy && !root.hasError
                    running: visible && Appearance.motion
                }
                ShellIcon {
                    source: root.success ? "dialog-ok-apply" : "security-high"
                    color: root.success ? root.successColor : root.errorColor
                    visible: root.hasError || root.success || root.phase === "unavailable"
                    Layout.preferredWidth: 15
                    Layout.preferredHeight: 15
                    Layout.alignment: Qt.AlignTop
                }
                AuthText {
                    objectName: "polkitMessage"
                    text: root.statusText
                    color: root.hasError || root.phase === "unavailable" || root.phase === "failed" ? root.errorColor : root.success ? root.successColor : Appearance.muted
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Math.round(18 * Appearance.textScale)
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Accessible.role: Accessible.StaticText
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 3
                implicitHeight: 1
                color: Appearance.outline
            }
            Button {
                id: detailsButton
                objectName: "polkitDetails"
                Layout.fillWidth: true
                implicitHeight: Math.max(36, contentItem.implicitHeight + 14)
                checkable: true
                checked: root.detailsOpen
                onClicked: root.detailsOpen = !root.detailsOpen
                Accessible.name: qsTr("Request details")
                KeyNavigation.tab: cancelButton
                KeyNavigation.backtab: passwordField.enabled ? (passwordField.revealButton.visible ? passwordField.revealButton : passwordField.field) : submitButton.enabled ? submitButton : cancelButton
                background: Rectangle {
                    color: "transparent"
                    radius: 4
                    border.width: detailsButton.visualFocus ? 1 : 0
                    border.color: Appearance.text
                }
                contentItem: RowLayout {
                    AuthText {
                        text: qsTr("Request details")
                        color: Appearance.muted
                        Layout.fillWidth: true
                    }
                    ShellIcon {
                        source: root.detailsOpen ? "go-up" : "go-down"
                        color: Appearance.muted
                        Layout.preferredWidth: 13
                        Layout.preferredHeight: 13
                    }
                }
            }
            Rectangle {
                visible: root.detailsOpen
                Layout.fillWidth: true
                implicitHeight: detailsLayout.implicitHeight + 24
                color: Qt.alpha(Appearance.recess, .5)
                radius: Appearance.radius * .4
                border.color: Appearance.outline
                GridLayout {
                    id: detailsLayout
                    anchors.fill: parent
                    anchors.margins: 12
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 8
                    AuthText {
                        text: qsTr("Action")
                        color: Appearance.muted
                        Layout.alignment: Qt.AlignTop
                    }
                    AuthText {
                        text: root.actionId
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    AuthText {
                        text: qsTr("Application")
                        color: Appearance.muted
                        Layout.alignment: Qt.AlignTop
                    }
                    AuthText {
                        text: root.requesterProgram || qsTr("Application information unavailable")
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                }
            }
        }
    }
    Item {
        id: footer
        anchors.bottom: parent.bottom
        width: parent.width
        implicitHeight: actions.implicitHeight + (Appearance.compact ? 24 : 30)
        height: implicitHeight
        Rectangle {
            width: parent.width
            height: 1
            color: Appearance.outline
        }
        RowLayout {
            id: actions
            anchors.fill: parent
            anchors.leftMargin: root.contentPadding
            anchors.rightMargin: root.contentPadding
            anchors.topMargin: Appearance.compact ? 12 : 15
            anchors.bottomMargin: Appearance.compact ? 12 : 15
            spacing: 10
            AuthText {
                text: qsTr("Esc to cancel")
                color: Appearance.muted
                visible: root.width >= 420
                Layout.fillWidth: true
            }
            Item {
                visible: root.width < 420
                Layout.fillWidth: true
            }
            PolkitAction {
                id: cancelButton
                objectName: "polkitCancel"
                text: root.success ? qsTr("Close") : qsTr("Cancel")
                Layout.minimumWidth: 0
                Layout.maximumWidth: Math.max(0, actions.width * .42)
                onClicked: root.cancel()
                KeyNavigation.tab: submitButton.enabled ? submitButton : root.firstControl
                KeyNavigation.backtab: detailsButton
            }
            PolkitAction {
                id: submitButton
                objectName: "polkitSubmit"
                primary: true
                text: root.canRetry ? qsTr("Try again") : root.success ? qsTr("Authenticated") : root.busy ? qsTr("Authenticating…") : qsTr("Authenticate")
                Layout.minimumWidth: 0
                Layout.maximumWidth: Math.max(0, actions.width * .58 - 10)
                enabled: root.canRetry || (root.inputReady && passwordField.text.length > 0)
                opacity: root.busy || root.success || enabled ? 1 : .4
                onClicked: {
                    if (root.canRetry) {
                        root.resetInput();
                        root.agent.retry();
                    } else
                        root.submit();
                }
                KeyNavigation.tab: root.firstControl
                KeyNavigation.backtab: cancelButton
            }
        }
    }
}
