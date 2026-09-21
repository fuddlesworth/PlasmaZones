// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Basic
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property string title: ""
    property string footerText: ""
    property string footerIcon: "security-high"
    property real railT: 0.5
    property real panelWidth: 410
    property real maxBodyHeight: 660
    property bool embedded: false
    property Component headerAction: null
    property var cancelTask: null
    default property alias content: body.data
    signal backRequested
    signal closeRequested
    implicitWidth: panelWidth
    implicitHeight: header.implicitHeight + (Appearance.compact ? 32 : 36) + 28 + Math.min(body.implicitHeight + 2 * bodyPadding, maxBodyHeight) + footer.implicitHeight + 2
    readonly property real bodyPadding: Appearance.compact ? 14 : 18
    focus: true
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    function goBack(): void {
        if (root.cancelTask)
            root.cancelTask();
        root.backRequested();
    }
    Keys.onEscapePressed: event => {
        if (!root.cancelTask || !root.cancelTask())
            root.backRequested();
        event.accepted = true;
    }
    Component.onCompleted: back.forceActiveFocus()
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged(): void {
            const item = root.Window.window.activeFocusItem;
            let ancestor = item;
            while (ancestor && ancestor !== body)
                ancestor = ancestor.parent;
            if (!ancestor || !item)
                return;
            const position = item.mapToItem(scroller, 0, 0);
            const offset = position.y < 8 ? position.y - 8 : Math.max(0, position.y + item.height - scroller.height + 8);
            scroller.contentY = Math.max(0, Math.min(Math.max(0, scroller.contentHeight - scroller.height), scroller.contentY + offset));
        }
    }
    ShellSurface {
        anchors.fill: parent
        railT: root.railT
        accented: true
        shadowed: !root.embedded
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            id: header
            Layout.fillWidth: true
            Layout.margins: Appearance.compact ? 16 : 18
            spacing: 10
            ShellButton {
                id: back
                objectName: "detailBack"
                iconName: "go-previous-symbolic"
                label: qsTr("Back to quick settings")
                implicitWidth: 34
                implicitHeight: 34
                flat: true
                onClicked: root.goBack()
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                DetailText {
                    Layout.fillWidth: true
                    text: qsTr("QUICK SETTINGS")
                    size: 9
                    muted: true
                    font.letterSpacing: 1.6
                }
                DetailText {
                    Layout.fillWidth: true
                    text: root.title
                    size: 21
                    font.weight: Font.Medium
                }
            }
            Loader {
                active: root.headerAction !== null
                sourceComponent: root.headerAction
            }
            ShellButton {
                iconName: "window-close-symbolic"
                label: qsTr("Close quick settings")
                implicitWidth: 34
                implicitHeight: 34
                flat: true
                onClicked: {
                    if (root.cancelTask)
                        root.cancelTask();
                    root.closeRequested();
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Appearance.outline
        }
        Flickable {
            id: scroller
            objectName: "detailScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            contentWidth: width
            contentHeight: body.implicitHeight + root.bodyPadding * 2
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            Basic.ScrollBar.vertical: Basic.ScrollBar {
                active: scroller.interactive
            }
            Column {
                id: body
                x: root.bodyPadding
                y: root.bodyPadding
                width: Math.max(0, scroller.width - root.bodyPadding * 2)
                spacing: 14
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Appearance.outline
        }
        RowLayout {
            id: footer
            Layout.fillWidth: true
            Layout.margins: 14
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 7
            ShellIcon {
                source: root.footerIcon
                color: Appearance.muted
                isMask: true
                Layout.preferredWidth: 13
                Layout.preferredHeight: 13
            }
            DetailText {
                Layout.fillWidth: true
                text: root.footerText
                size: 10
                muted: true
            }
            DetailText {
                text: "PHOSPHOR"
                size: 8
                muted: true
                font.letterSpacing: 1.6
            }
        }
    }
}
