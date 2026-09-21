// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property var entries: []
    property int barCount: 0
    property var shownItems: TrayModel.barItems
    property string selectedKey: ""
    property bool settingsOpen: false
    property real maximumHeight: 760
    property real railT: 0.8
    readonly property var attention: TrayModel.items.find(entry => entry.attention && entry.visibility !== "hidden")
    signal menuRequested(var entry, Item anchor)
    signal closeRequested
    signal settingsRequested(bool open)
    signal activated
    implicitWidth: settingsOpen ? 452 : 366
    implicitHeight: Math.min(maximumHeight, header.implicitHeight + 40 + footer.implicitHeight + 24 + Math.min(settingsOpen ? 580 : 374, body.implicitHeight + 32) + 2)
    function focusFirst(): void {
        if (!settingsOpen && tiles.count)
            tiles.itemAt(0).focusPrimary();
        else if (settingsOpen)
            backButton.forceActiveFocus();
        else
            settingsButton.forceActiveFocus();
    }
    function focusEntry(key: string): void {
        for (let index = 0; index < tiles.count; ++index) {
            const tile = tiles.itemAt(index);
            if (tile && tile.entry.instanceKey === key) {
                tile.focusPrimary();
                return;
            }
        }
        focusFirst();
    }
    function navigate(direction: int): void {
        let found = -1;
        let focused = root.Window.window ? root.Window.window.activeFocusItem : null;
        while (focused && focused.parent !== grid)
            focused = focused.parent;
        for (let i = 0; i < tiles.count; ++i)
            if (tiles.itemAt(i) === focused) {
                found = i;
                break;
            }
        const next = Math.max(0, Math.min(tiles.count - 1, found < 0 ? 0 : found + direction));
        if (tiles.itemAt(next))
            tiles.itemAt(next).focusPrimary();
    }
    Keys.onPressed: event => {
        if (settingsOpen || ![Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down].includes(event.key))
            return;
        navigate(event.key === Qt.Key_Left ? -1 : event.key === Qt.Key_Right ? 1 : event.key === Qt.Key_Up ? -3 : 3);
        event.accepted = true;
    }
    ShellSurface {
        anchors.fill: parent
        accented: true
        railT: root.railT
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            id: header
            Layout.fillWidth: true
            Layout.margins: 19
            Layout.topMargin: 21
            spacing: 12
            ShellIcon {
                visible: !root.settingsOpen
                source: "phosphor-tray"
                color: Appearance.accent
                Layout.preferredWidth: 36
                Layout.preferredHeight: 29
            }
            TrayButton {
                id: backButton
                visible: root.settingsOpen
                iconName: "go-previous-symbolic"
                label: qsTr("Back to background apps")
                onClicked: root.settingsRequested(false)
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                DetailText {
                    Layout.fillWidth: true
                    text: root.settingsOpen ? qsTr("MAKE IT YOURS") : qsTr("SYSTEM TRAY")
                    size: 9
                    font.letterSpacing: 1.6
                    muted: true
                }
                DetailText {
                    Layout.fillWidth: true
                    text: root.settingsOpen ? qsTr("Arrange tray") : qsTr("Background apps")
                    size: Appearance.compact ? 20 : 22
                    font.letterSpacing: -0.6
                }
            }
            TrayButton {
                id: settingsButton
                objectName: "traySettings"
                visible: !root.settingsOpen
                iconName: "preferences-system"
                label: qsTr("Arrange tray")
                onClicked: root.settingsRequested(true)
            }
            TrayButton {
                objectName: "trayClose"
                iconName: "window-close"
                label: qsTr("Close system tray")
                onClicked: root.closeRequested()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Appearance.outline
        }
        Flickable {
            id: scroller
            objectName: "trayScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            contentWidth: width
            contentHeight: body.implicitHeight + 32
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Basic.ScrollBar.vertical: Basic.ScrollBar {
                policy: scroller.contentHeight > scroller.height + 1 ? Basic.ScrollBar.AsNeeded : Basic.ScrollBar.AlwaysOff
            }
            Column {
                id: body
                x: root.settingsOpen ? 20 : 16
                y: 16
                width: scroller.width - x * 2
                spacing: 14
                Loader {
                    active: root.settingsOpen
                    visible: active
                    width: parent.width
                    sourceComponent: Component {
                        TraySettings {
                            shownItems: root.shownItems
                        }
                    }
                }
                Basic.AbstractButton {
                    id: attentionCard
                    visible: !root.settingsOpen && !!root.attention
                    width: parent.width
                    implicitHeight: 75
                    Accessible.name: root.attention ? qsTr("%1 needs attention").arg(TrayModel.title(root.attention)) : ""
                    background: Rectangle {
                        radius: Appearance.radius * 0.55
                        color: Qt.tint(Appearance.card, Qt.alpha(Appearance.stops[3], 0.08))
                        border.width: 1
                        border.color: attentionCard.visualFocus ? Appearance.text : Qt.alpha(Appearance.stops[3], 0.32)
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        TrayIcon {
                            entry: root.attention || ({})
                            Layout.leftMargin: 12
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                            tint: Appearance.stops[3]
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            DetailText {
                                Layout.fillWidth: true
                                text: root.attention ? TrayModel.title(root.attention) : ""
                                color: Appearance.stops[3]
                                size: 12
                            }
                            DetailText {
                                Layout.fillWidth: true
                                text: root.attention ? TrayModel.status(root.attention) : ""
                                size: 10
                                muted: true
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                        ShellIcon {
                            source: "go-next-symbolic"
                            color: Appearance.stops[3]
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            Layout.rightMargin: 12
                        }
                    }
                    onClicked: root.menuRequested(root.attention, attentionCard)
                }
                Grid {
                    id: grid
                    visible: !root.settingsOpen && root.entries.length > 0
                    width: parent.width
                    columns: 3
                    spacing: 6
                    Repeater {
                        id: tiles
                        model: root.settingsOpen ? [] : root.entries
                        delegate: TrayTile {
                            required property var modelData
                            width: (body.width - 12) / 3
                            height: implicitHeight
                            entry: modelData
                            selected: entry.instanceKey === root.selectedKey
                            onMenuRequested: (entry, anchor) => root.menuRequested(entry, anchor)
                            onActivated: root.activated()
                        }
                    }
                }
                Column {
                    visible: !root.settingsOpen && !root.entries.length
                    width: parent.width
                    topPadding: 24
                    bottomPadding: 24
                    spacing: 12
                    ShellIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 34
                        height: 34
                        source: "phosphor-tray"
                        color: Appearance.accent
                    }
                    DetailText {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: root.barCount ? qsTr("Everything is in your bar") : qsTr("A quiet corner")
                        size: 19
                    }
                    DetailText {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: root.barCount ? qsTr("More background apps will appear here.") : qsTr("Background apps will appear here when they’re running.")
                        muted: true
                        size: 12
                    }
                }
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
            Layout.margins: 11
            Layout.leftMargin: 17
            Layout.rightMargin: 17
            DetailText {
                visible: !root.settingsOpen
                text: qsTr("%1 in overflow · %2 in bar").arg(root.entries.length).arg(root.barCount)
                size: 10
                muted: true
            }
            ShellButton {
                visible: root.settingsOpen
                flat: true
                text: qsTr("Reset tray")
                onClicked: TrayModel.reset()
            }
            Item {
                Layout.fillWidth: true
            }
            ShellButton {
                objectName: "trayArrange"
                visible: !root.settingsOpen
                flat: true
                text: qsTr("Arrange tray")
                iconName: "preferences-system"
                onClicked: root.settingsRequested(true)
            }
            ShellButton {
                visible: root.settingsOpen
                highlighted: true
                text: qsTr("Done")
                onClicked: root.settingsRequested(false)
            }
        }
    }
    onSettingsOpenChanged: scroller.contentY = 0
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged(): void {
            const item = root.Window.window.activeFocusItem;
            let ancestor = item;
            while (ancestor && ancestor !== body)
                ancestor = ancestor.parent;
            if (!ancestor || !item)
                return;
            const point = item.mapToItem(scroller, 0, 0);
            const offset = point.y < 8 ? point.y - 8 : Math.max(0, point.y + item.height - scroller.height + 8);
            scroller.contentY = Math.max(0, Math.min(Math.max(0, scroller.contentHeight - scroller.height), scroller.contentY + offset));
        }
    }
}
