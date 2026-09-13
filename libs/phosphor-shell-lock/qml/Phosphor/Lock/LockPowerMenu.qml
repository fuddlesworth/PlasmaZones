// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root
    property var session: null
    property bool open: false
    property string pending: ""
    readonly property var actions: [
        {
            id: "suspend",
            label: qsTr("Sleep"),
            icon: "system-suspend",
            capability: "canSuspend"
        },
        {
            id: "reboot",
            label: qsTr("Restart"),
            icon: "system-reboot",
            capability: "canReboot"
        },
        {
            id: "powerOff",
            label: qsTr("Shut down"),
            icon: "system-shutdown",
            capability: "canPowerOff"
        }
    ]
    // Only Yes (1) is usable here. Challenge needs a polkit surface outside
    // ext-session-lock, which cannot be displayed above the locked session.
    function available(action) {
        return !!session && session[action.capability] === 1;
    }
    function close() {
        open = false;
        pending = "";
        closed();
    }
    function invoke(action, confirmed) {
        if (!available(action))
            return;
        if (action.id !== "suspend" && !confirmed) {
            pending = action.id;
            cancel.forceActiveFocus();
            return;
        }
        session[action.id]();
        close();
    }
    signal closed
    implicitWidth: trigger.implicitWidth
    implicitHeight: trigger.implicitHeight
    onEnabledChanged: if (!enabled)
        close()
    ShellButton {
        id: trigger
        objectName: "lockPowerButton"
        implicitHeight: 40
        text: qsTr("Power")
        iconName: "system-shutdown"
        flat: true
        outlined: true
        cornerRadius: Appearance.radius * .55
        onClicked: {
            if (root.open)
                root.close();
            else {
                if (root.session)
                    root.session.refreshCapabilities();
                root.open = true;
            }
        }
    }
    ShellSurface {
        anchors.right: parent.right
        anchors.bottom: parent.top
        anchors.bottomMargin: 16
        visible: root.open
        width: root.pending ? 254 : 210
        height: body.implicitHeight + 32
        ColumnLayout {
            id: body
            x: 16
            y: 16
            width: parent.width - 32
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: root.pending ? (root.pending === "reboot" ? qsTr("Restart your computer?") : qsTr("Shut down your computer?")) : qsTr("POWER")
                font.family: Tokens.font_family_ui
                font.pixelSize: root.pending ? 14 : 8
                font.letterSpacing: root.pending ? 0 : 1.6
                color: root.pending ? Appearance.text : Appearance.muted
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: root.actions
                ShellButton {
                    required property var modelData
                    objectName: "lockAction-" + modelData.id
                    Layout.fillWidth: true
                    implicitHeight: 38
                    visible: !root.pending && root.available(modelData)
                    text: modelData.label
                    iconName: modelData.icon
                    flat: true
                    labelSize: 12
                    onClicked: root.invoke(modelData, false)
                }
            }
            Text {
                visible: !root.pending && !root.actions.some(a => root.available(a))
                Layout.fillWidth: true
                text: qsTr("No power actions are available while locked.")
                wrapMode: Text.WordWrap
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 11
            }
            Text {
                visible: root.pending !== ""
                Layout.fillWidth: true
                text: qsTr("Unsaved work may be lost.")
                wrapMode: Text.WordWrap
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 11
            }
            RowLayout {
                visible: root.pending !== ""
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                ShellButton {
                    id: cancel
                    objectName: "lockPowerCancel"
                    text: qsTr("Cancel")
                    onClicked: root.close()
                }
                ShellButton {
                    objectName: "lockPowerConfirm"
                    text: root.pending === "reboot" ? qsTr("Restart") : qsTr("Shut down")
                    onClicked: {
                        const action = root.actions.find(a => a.id === root.pending);
                        if (action)
                            root.invoke(action, true);
                    }
                }
            }
        }
    }
}
