// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root
    property alias text: field.text
    property alias field: field
    property alias revealButton: reveal
    property Item nextFocusItem: null
    property Item previousFocusItem: null
    property string label: qsTr("Password")
    property bool echo: false
    property bool invalid: false
    property bool revealed: false
    readonly property color edgeColor: invalid ? (Appearance.light ? "#a32742" : "#fda2a4") : Appearance.accent
    signal accepted
    signal edited
    implicitHeight: Math.max(46, field.implicitHeight + 6)
    function clear(): void {
        field.clear();
        revealed = false;
    }
    onEchoChanged: clear()
    onEnabledChanged: if (!enabled)
        clear()
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: Appearance.radius * .5 + 3
        color: Qt.alpha(root.edgeColor, .15)
        border.width: 1
        border.color: Qt.alpha(root.edgeColor, .48)
        visible: field.activeFocus
    }
    Rectangle {
        anchors.fill: parent
        radius: Appearance.radius * .5
        color: Appearance.recess
        border.width: 1
        border.color: field.activeFocus || root.invalid ? root.edgeColor : Appearance.outline
    }
    TextField {
        id: field
        objectName: "polkitField"
        anchors.fill: parent
        anchors.margins: 3
        rightPadding: reveal.visible ? reveal.width + 7 : 10
        leftPadding: 10
        topPadding: 5
        bottomPadding: 5
        color: Appearance.text
        font.family: Tokens.font_family_ui
        font.pixelSize: Math.round(16 * Appearance.textScale)
        echoMode: root.echo || root.revealed ? TextInput.Normal : TextInput.Password
        passwordCharacter: "•"
        passwordMaskDelay: 0
        inputMethodHints: root.echo ? Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText : Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        selectByMouse: true
        clip: true
        background: null
        Accessible.name: root.label
        // Revealing a password is visual only; assistive APIs still mark it as secret.
        Accessible.passwordEdit: !root.echo
        KeyNavigation.tab: reveal.visible ? reveal : root.nextFocusItem
        KeyNavigation.backtab: root.previousFocusItem
        onAccepted: root.accepted()
        onTextEdited: root.edited()
        Text {
            anchors.left: parent.left
            anchors.leftMargin: field.leftPadding
            anchors.right: parent.right
            anchors.rightMargin: field.rightPadding
            anchors.verticalCenter: parent.verticalCenter
            visible: field.text.length === 0
            text: root.echo ? qsTr("Enter your response") : qsTr("Enter your password")
            textFormat: Text.PlainText
            color: Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: Math.round(12 * Appearance.textScale)
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }
    ShellButton {
        id: reveal
        objectName: "polkitReveal"
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        visible: !root.echo
        width: 36
        height: 36
        flat: true
        iconName: "view-visible"
        label: root.revealed ? qsTr("Hide password") : qsTr("Show password")
        checkable: true
        checked: root.revealed
        KeyNavigation.tab: root.nextFocusItem
        KeyNavigation.backtab: field
        onClicked: root.revealed = !root.revealed
    }
}
