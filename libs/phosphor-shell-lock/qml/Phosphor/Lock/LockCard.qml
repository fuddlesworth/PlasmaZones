// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property var controller: null
    property string userName: ""
    property var keyboard: null
    readonly property int inset: Appearance.compact ? 26 : 32
    readonly property string phase: controller ? controller.phase : "idle"
    implicitWidth: 382
    implicitHeight: content.implicitHeight + inset * 2
    function focusPassword() {
        field.forceActiveFocus();
    }
    ShellSurface {
        anchors.fill: parent
        accented: true
    }
    ColumnLayout {
        id: content
        x: root.inset
        y: root.inset
        width: parent.width - root.inset * 2
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: Appearance.compact ? 24 : 32
            spacing: 16
            Rectangle {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                radius: Appearance.radius * .9
                border.width: 1
                border.color: Qt.alpha(Appearance.stops[2], .35)
                gradient: Gradient {
                    GradientStop {
                        position: 0
                        color: Qt.tint(Appearance.recess, Qt.alpha(Appearance.stops[1], .25))
                    }
                    GradientStop {
                        position: 1
                        color: Qt.tint(Appearance.recess, Qt.alpha(Appearance.stops[3], .16))
                    }
                }
                Text {
                    anchors.centerIn: parent
                    text: root.userName ? Array.from(root.userName)[0] : "φ"
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 26
                }
                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: -5
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 2
                    width: 13
                    height: 13
                    radius: 7
                    border.width: 3
                    border.color: Appearance.surface
                    color: Appearance.stops[0]
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: qsTr("WELCOME BACK")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 8
                    font.letterSpacing: 1.6
                }
                Text {
                    Layout.fillWidth: true
                    text: root.userName
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 21
                    font.weight: Font.Medium
                    font.letterSpacing: -.4
                    elide: Text.ElideRight
                }
            }
            ShellIcon {
                Layout.preferredWidth: 17
                Layout.preferredHeight: 17
                source: root.phase === "dismissing" ? "object-unlocked" : "object-locked"
                color: root.phase === "dismissing" ? Appearance.stops[0] : Appearance.muted
            }
        }
        Text {
            Layout.bottomMargin: 10
            text: qsTr("Password")
            color: Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: 11
        }
        LockAuthField {
            id: field
            objectName: "lockPassword"
            Layout.fillWidth: true
            controller: root.controller
            focus: true
        }
        Text {
            objectName: "lockMessage"
            Layout.fillWidth: true
            Layout.minimumHeight: 37
            topPadding: 5
            text: root.phase === "authenticating" ? qsTr("Unlocking your session…") : root.phase === "dismissing" ? qsTr("You’re back.") : root.phase === "error" ? root.controller.errorText : qsTr("Enter to unlock")
            color: root.phase === "error" ? field.edgeColor : Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: 10
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: Appearance.compact ? 3 : 8
            height: 1
            color: Appearance.outline
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Appearance.compact ? 12 : 17
            spacing: 8
            ShellButton {
                objectName: "lockKeyboard"
                flat: true
                implicitHeight: 26
                implicitWidth: contentItem.implicitWidth
                visible: !!root.keyboard && root.keyboard.layoutName !== ""
                enabled: !!root.keyboard && root.keyboard.canCycle
                iconName: "input-keyboard"
                text: root.keyboard ? root.keyboard.layoutName + (root.keyboard.canCycle ? "  ⌄" : "") : ""
                label: qsTr("Keyboard layout") + ": " + text
                labelSize: 10
                foreground: Appearance.muted
                onClicked: {
                    root.keyboard.nextLayout();
                    root.focusPassword();
                }
            }
            Item {
                Layout.fillWidth: true
            }
            Text {
                objectName: "lockCaps"
                readonly property bool caps: !!root.keyboard && root.keyboard.capsLock
                text: caps ? qsTr("⇪ Caps Lock") : qsTr("Esc to clear")
                color: caps ? Appearance.stops[3] : Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 10
            }
        }
    }
}
