// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import Phosphor.Theme

Basic.Button {
    id: root
    required property var entry
    property bool showPercentage: false
    property bool expanded: false
    property string activationDescription: i18n("Open quick settings")
    signal contextRequested
    readonly property bool percentageVisible: showPercentage && entry.id === "battery" && entry.percent >= 0
    implicitWidth: contentItem.implicitWidth + 2 * Tokens.spacing_s
    implicitHeight: 30
    padding: Tokens.spacing_s
    Accessible.name: entry.summary ? i18n("%1, %2. %3").arg(entry.title).arg(entry.summary).arg(activationDescription) : i18n("%1. %2").arg(entry.title).arg(activationDescription)
    Accessible.description: expanded ? i18n("Controls are open. Right-click or press Shift+F10 for controls.") : i18n("Right-click or press Shift+F10 for controls.")
    Accessible.pressed: expanded
    Basic.ToolTip.visible: hovered
    Basic.ToolTip.text: entry.title + (entry.summary ? " · " + entry.summary : "") + "\n" + i18n("Right-click for controls")
    background: Rectangle {
        radius: Tokens.radius_s
        color: root.hovered || root.expanded || root.down ? Qt.alpha(Appearance.stops[1], 0.18) : "transparent"
        border.width: root.visualFocus ? 1 : 0
        border.color: Appearance.text
    }
    contentItem: Row {
        spacing: Tokens.spacing_xs
        StatusIcon {
            anchors.verticalCenter: parent.verticalCenter
            entry: root.entry
            width: implicitWidth
            height: implicitHeight
        }
        Text {
            visible: root.percentageVisible
            anchors.verticalCenter: parent.verticalCenter
            text: root.percentageVisible ? i18n("%1%").arg(Math.round(root.entry.percent)) : ""
            color: Appearance.text
            font.family: Tokens.font_family_mono
            font.pixelSize: Tokens.font_size_label_s
        }
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: root.contextRequested()
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier)) {
            root.contextRequested();
            event.accepted = true;
        }
    }
}
