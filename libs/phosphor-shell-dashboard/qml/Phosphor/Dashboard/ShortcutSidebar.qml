// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root
    property string scope: "tiling"
    property string currentMode: "tiling"
    property string workspaceName: ""
    property bool horizontal: false
    signal chosen(string scope)
    readonly property var sections: [
        {
            id: "tiling",
            label: qsTr("Tiling"),
            icon: "view-split-left-right",
            color: Appearance.stops[1]
        },
        {
            id: "scrolling",
            label: qsTr("Scrolling"),
            icon: "view-split-left-right",
            color: Appearance.stops[2]
        },
        {
            id: "snapping",
            label: qsTr("Snapping"),
            icon: "view-grid",
            color: Appearance.stops[0]
        },
        {
            id: "general",
            label: qsTr("General"),
            icon: "configure",
            color: Appearance.stops[1]
        },
        {
            id: "shell",
            label: qsTr("Shell"),
            icon: "input-keyboard",
            color: Appearance.stops[3]
        },
        {
            id: "all",
            label: qsTr("All shortcuts"),
            icon: "edit-find",
            color: Appearance.stops[1]
        }
    ]
    readonly property string currentModeLabel: (sections.find(section => section.id === currentMode) || {
            label: qsTr("No placement")
        }).label
    Row {
        id: brand
        visible: !root.horizontal
        x: 22
        y: 27
        spacing: 9
        PhosphorMark {
            width: 37
            height: 37
        }
        Column {
            spacing: 3
            anchors.verticalCenter: parent.verticalCenter
            ShortcutText {
                text: "PHOSPHOR"
                size: 9
                kicker: true
                color: Appearance.text
                font.weight: Font.DemiBold
            }
            ShortcutText {
                text: qsTr("At your fingertips.")
                width: root.width - 93
                size: 10
                muted: true
            }
        }
    }
    ShortcutText {
        x: 27
        y: 95
        visible: !root.horizontal
        text: qsTr("BROWSE SHORTCUTS")
        size: 9
        kicker: true
    }
    Flickable {
        id: navViewport
        x: root.horizontal ? 12 : 15
        y: root.horizontal ? 8 : 120
        width: root.width - x * 2
        height: root.horizontal ? root.height - 16 : Math.max(0, (context.visible ? context.y - 15 : root.height - 15) - y)
        contentWidth: root.horizontal ? navigation.implicitWidth : width
        contentHeight: navigation.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        GridLayout {
            id: navigation
            columns: root.horizontal ? root.sections.length : 1
            width: root.horizontal ? implicitWidth : navViewport.width
            rowSpacing: 5
            columnSpacing: 5
            Repeater {
                id: items
                model: root.sections
                delegate: Basic.AbstractButton {
                    id: button
                    required property var modelData
                    required property int index
                    objectName: "shortcutScope:" + modelData.id
                    Layout.fillWidth: !root.horizontal
                    Layout.preferredWidth: root.horizontal ? contentItem.implicitWidth + 24 : -1
                    Layout.minimumHeight: 43
                    Layout.topMargin: !root.horizontal && index === 3 ? 20 : 0
                    leftPadding: 12
                    rightPadding: 12
                    topPadding: 10
                    bottomPadding: 10
                    checkable: true
                    autoExclusive: true
                    checked: root.scope === modelData.id
                    Accessible.name: modelData.label
                    Accessible.description: modelData.id === root.currentMode ? qsTr("Current workspace mode") : ""
                    onClicked: root.chosen(modelData.id)
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            event.accepted = true;
                            if (!event.isAutoRepeat)
                                button.click();
                            return;
                        }
                        let target = index;
                        if (event.key === Qt.Key_Down || event.key === Qt.Key_Right)
                            target = (index + 1) % items.count;
                        else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left)
                            target = (index + items.count - 1) % items.count;
                        else if (event.key === Qt.Key_Home)
                            target = 0;
                        else if (event.key === Qt.Key_End)
                            target = items.count - 1;
                        else
                            return;
                        event.accepted = true;
                        items.itemAt(target).forceActiveFocus(Qt.TabFocusReason);
                    }
                    onActiveFocusChanged: if (activeFocus) {
                        const point = mapToItem(navigation, 0, 0);
                        if (root.horizontal)
                            navViewport.contentX = Math.max(0, Math.min(point.x, navViewport.contentWidth - navViewport.width));
                        else if (point.y < navViewport.contentY || point.y + height > navViewport.contentY + navViewport.height)
                            navViewport.contentY = Math.max(0, Math.min(point.y, navViewport.contentHeight - navViewport.height));
                    }
                    background: Rectangle {
                        radius: Appearance.radius * .52
                        color: button.checked ? Qt.tint(Appearance.card, Qt.alpha(button.modelData.color, .17)) : button.hovered ? Qt.alpha(Appearance.card, .35) : "transparent"
                        border.color: button.visualFocus ? button.modelData.color : "transparent"
                        Rectangle {
                            x: 0
                            y: 13
                            width: 2
                            height: parent.height - 26
                            radius: 1
                            color: button.modelData.color
                            visible: button.checked
                        }
                    }
                    contentItem: RowLayout {
                        spacing: 11
                        ShellIcon {
                            Layout.preferredWidth: 17
                            Layout.preferredHeight: 17
                            source: button.modelData.icon
                            color: button.checked ? Appearance.text : Appearance.muted
                        }
                        ShortcutText {
                            Layout.fillWidth: !root.horizontal
                            text: button.modelData.label
                            muted: !button.checked
                            size: 12
                        }
                        Rectangle {
                            visible: button.modelData.id === root.currentMode
                            implicitWidth: 5
                            implicitHeight: 5
                            radius: 3
                            color: button.modelData.color
                            Accessible.ignored: true
                        }
                    }
                }
            }
        }
    }
    Column {
        id: context
        x: 27
        width: root.width - 54
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        visible: !root.horizontal && root.height > 600
        spacing: 7
        SpectrumRail {
            width: 36
            height: 2
        }
        ShortcutText {
            width: parent.width
            text: qsTr("THIS WORKSPACE")
            kicker: true
            size: 9
            topPadding: 6
        }
        ShortcutText {
            width: parent.width
            text: root.workspaceName.length ? qsTr("%1 · %2").arg(root.workspaceName).arg(root.currentModeLabel) : root.currentModeLabel
            size: 12
        }
        ShortcutText {
            width: parent.width
            text: qsTr("Browse any mode.\nYour windows stay in place.")
            size: 10
            lineHeight: 1.4
            muted: true
        }
    }
}
