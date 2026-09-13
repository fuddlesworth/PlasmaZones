// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property var controller: null
    readonly property string phase: controller ? controller.phase : "idle"
    readonly property int dotCount: controller ? Array.from(controller.password).length : 0
    readonly property color edgeColor: phase === "error" ? (Appearance.light ? "#a32742" : "#fda2a4") : Appearance.accent
    implicitWidth: 318
    implicitHeight: 52
    activeFocusOnTab: true
    Accessible.role: Accessible.EditableText
    Accessible.name: qsTr("Password")
    Accessible.passwordEdit: true
    Accessible.description: controller ? controller.errorText : ""
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: Appearance.radius * .55 + 3
        color: Qt.alpha(root.edgeColor, .1)
        visible: root.activeFocus || root.phase === "error"
    }
    Rectangle {
        anchors.fill: parent
        radius: Appearance.radius * .55
        color: Appearance.recess
        border.width: 1
        border.color: root.activeFocus || root.phase !== "idle" ? root.edgeColor : Appearance.outline
    }
    // Only masked text is rendered or exposed to accessibility. The controller
    // holds one password across outputs; long passwords stay inside the field.
    Item {
        x: 16
        y: 1
        width: Math.max(0, submit.x - x - 10)
        height: parent.height - 2
        clip: true
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            text: root.dotCount ? "•".repeat(Math.min(root.dotCount, 512)) : qsTr("Enter your password")
            horizontalAlignment: implicitWidth > width ? Text.AlignRight : Text.AlignLeft
            color: root.dotCount ? Appearance.text : Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: root.dotCount ? 14 : 11
            font.letterSpacing: root.dotCount ? 2 : 0
            opacity: root.phase === "authenticating" ? .55 : 1
            Accessible.ignored: true
        }
        MouseArea {
            anchors.fill: parent
            onClicked: root.forceActiveFocus()
        }
    }
    ShellButton {
        id: submit
        objectName: "lockSubmit"
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: 40
        implicitHeight: 40
        cornerRadius: Appearance.radius * .35
        label: qsTr("Unlock")
        opacity: root.phase === "authenticating" || enabled ? 1 : .45
        enabled: !!root.controller && root.controller.locked && root.dotCount > 0 && root.phase !== "authenticating" && root.phase !== "dismissing"
        onClicked: root.controller.submit()
        contentItem: Item {
            ShellIcon {
                anchors.centerIn: parent
                width: 18
                height: 18
                source: "arrow-right"
                color: Appearance.text
                visible: root.phase !== "authenticating"
            }
            Canvas {
                id: spinner
                anchors.centerIn: parent
                width: 16
                height: 16
                visible: root.phase === "authenticating"
                onVisibleChanged: if (visible)
                    requestPaint()
                Connections {
                    target: Appearance
                    function onAccentChanged() {
                        spinner.requestPaint();
                    }
                }
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    ctx.lineWidth = 2;
                    ctx.strokeStyle = Appearance.outline;
                    ctx.beginPath();
                    ctx.arc(8, 8, 6, 0, Math.PI * 2);
                    ctx.stroke();
                    ctx.strokeStyle = Appearance.accent;
                    ctx.beginPath();
                    ctx.arc(8, 8, 6, -Math.PI / 2, 0);
                    ctx.stroke();
                }
                RotationAnimator {
                    target: spinner
                    from: 0
                    to: 360
                    duration: 800
                    loops: Animation.Infinite
                    running: spinner.visible && Appearance.motion
                }
            }
        }
    }
}
