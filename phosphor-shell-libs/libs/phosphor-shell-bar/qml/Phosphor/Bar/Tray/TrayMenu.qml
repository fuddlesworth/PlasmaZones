// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Service.Sni

FocusScope {
    id: root
    required property var entry
    property real railT: 0.8
    property real maximumHeight: 600
    property var path: []
    property string failure: ""
    property bool pendingFocus: true
    property bool ready: false
    readonly property string menuSource: (entry.dbusService || "") + "|" + (entry.menuPath || "")
    readonly property bool canGoBack: path.length > 0
    signal closeRequested
    signal settingsRequested
    signal nativeMenuRequested(var entry, var point)
    implicitWidth: 300
    implicitHeight: Math.min(maximumHeight, column.implicitHeight)
    function back(): void {
        if (!path.length) {
            closeRequested();
            return;
        }
        const stack = path.slice();
        stack.pop();
        path = stack;
        pendingFocus = true;
        menuModel.rootId = stack.length ? stack[stack.length - 1].id : 0;
    }
    function navigate(from: int, direction: int): void {
        for (let step = 1; step <= rows.count; ++step) {
            const at = (from + direction * step + rows.count * 2) % rows.count;
            const row = rows.itemAt(at);
            if (row && row.visible && row.actionable) {
                row.focusButton();
                return;
            }
        }
    }
    function activate(row: int, title: string, submenu: bool, toggle: bool): void {
        if (submenu) {
            const id = menuModel.aboutToShowSubmenu(row);
            if (id < 0)
                return;
            path = path.concat([
                {
                    id: id,
                    title: title
                }
            ]);
            pendingFocus = true;
            menuModel.rootId = id;
        } else {
            menuModel.triggerItem(row);
            if (!toggle)
                closeRequested();
        }
    }
    function syncSource(): void {
        menuModel.aboutToHide();
        path = [];
        failure = "";
        pendingFocus = true;
        menuModel.service = entry.dbusService || "";
        menuModel.path = entry.menuPath || "";
        menuModel.rootId = 0;
        menuModel.aboutToShow();
        menuModel.refresh();
    }
    onMenuSourceChanged: if (ready)
        syncSource()
    Component.onCompleted: {
        ready = true;
        syncSource();
    }
    Component.onDestruction: menuModel.aboutToHide()
    Keys.onLeftPressed: event => {
        back();
        event.accepted = true;
    }
    DBusMenuModel {
        id: menuModel
        objectName: "trayMenuModel"
        onLoaded: {
            root.failure = "";
            if (root.pendingFocus)
                focusTimer.restart();
        }
        onLoadFailed: message => {
            root.failure = qsTr("This app’s menu could not be loaded.");
        }
    }
    Timer {
        id: focusTimer
        interval: 0
        onTriggered: {
            root.navigate(-1, 1);
            root.pendingFocus = false;
        }
    }
    ShellSurface {
        anchors.fill: parent
        railT: root.railT
        radius: Appearance.radius * 0.7
    }
    ColumnLayout {
        id: column
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            spacing: 12
            TrayButton {
                visible: root.canGoBack
                iconName: "go-previous-symbolic"
                label: qsTr("Back to app menu")
                onClicked: root.back()
            }
            TrayIcon {
                visible: !root.canGoBack
                entry: root.entry
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                tint: Appearance.accent
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                DetailText {
                    Layout.fillWidth: true
                    text: root.path.length ? root.path[root.path.length - 1].title : TrayModel.title(root.entry)
                    size: 15
                }
                DetailText {
                    Layout.fillWidth: true
                    text: root.path.length ? TrayModel.title(root.entry) : TrayModel.status(root.entry)
                    size: 10
                    muted: true
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
            TrayButton {
                iconName: "window-close"
                label: qsTr("Close app menu")
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
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: Math.max(40, Math.min(contentHeight, root.maximumHeight - 150))
            contentWidth: width
            contentHeight: menuColumn.implicitHeight + 16
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Basic.ScrollBar.vertical: Basic.ScrollBar {
                policy: scroller.contentHeight > scroller.height ? Basic.ScrollBar.AsNeeded : Basic.ScrollBar.AlwaysOff
            }
            Column {
                id: menuColumn
                x: 8
                y: 8
                width: scroller.width - 16
                spacing: 0
                Repeater {
                    id: rows
                    model: menuModel
                    delegate: Item {
                        id: row
                        required property int index
                        required property var model
                        readonly property bool actionable: model.itemEnabled && model.itemType !== "separator"
                        width: menuColumn.width
                        height: !model.itemVisible ? 0 : model.itemType === "separator" ? 13 : button.implicitHeight
                        visible: model.itemVisible
                        function focusButton(): void {
                            button.forceActiveFocus();
                        }
                        Rectangle {
                            visible: row.model.itemType === "separator"
                            x: 10
                            width: parent.width - 20
                            anchors.verticalCenter: parent.verticalCenter
                            height: 1
                            color: Appearance.outline
                        }
                        Basic.AbstractButton {
                            id: button
                            objectName: "trayMenuItem-" + row.model.menuId
                            visible: row.model.itemType !== "separator"
                            enabled: row.model.itemEnabled
                            width: parent.width
                            implicitHeight: Math.max(Appearance.compact ? 33 : 37, label.implicitHeight + 14)
                            Accessible.name: row.model.label
                            Accessible.role: row.model.toggleType === "radio" ? Accessible.RadioButton : row.model.toggleType === "checkmark" ? Accessible.CheckBox : Accessible.MenuItem
                            Accessible.checked: row.model.toggleState === 1
                            opacity: enabled ? 1 : 0.4
                            background: Rectangle {
                                radius: 6
                                color: button.hovered || button.activeFocus ? Qt.tint(Appearance.card, Qt.alpha(Appearance.accent, 0.16)) : "transparent"
                                border.width: button.visualFocus ? 1 : 0
                                border.color: Appearance.text
                            }
                            contentItem: RowLayout {
                                spacing: 10
                                Item {
                                    Layout.leftMargin: 10
                                    Layout.preferredWidth: 18
                                    Layout.preferredHeight: 18
                                    DetailText {
                                        anchors.centerIn: parent
                                        visible: row.model.toggleType === "checkmark"
                                        text: row.model.toggleState === 1 ? "✓" : row.model.toggleState === -1 ? "−" : ""
                                        color: Appearance.accent
                                    }
                                    Rectangle {
                                        anchors.centerIn: parent
                                        visible: row.model.toggleType === "radio"
                                        width: 9
                                        height: 9
                                        radius: 5
                                        color: row.model.toggleState === 1 ? Appearance.accent : "transparent"
                                        border.width: 1
                                        border.color: Appearance.muted
                                    }
                                    Image {
                                        anchors.fill: parent
                                        visible: !row.model.toggleType && !!row.model.iconUrl
                                        source: visible ? row.model.iconUrl : ""
                                        sourceSize: Qt.size(36, 36)
                                        fillMode: Image.PreserveAspectFit
                                    }
                                }
                                DetailText {
                                    id: label
                                    Layout.fillWidth: true
                                    text: row.model.label
                                    size: 13
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                }
                                DetailText {
                                    visible: !!row.model.shortcut
                                    text: row.model.shortcut || ""
                                    muted: true
                                    size: 9
                                    Layout.maximumWidth: 64
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                                ShellIcon {
                                    visible: row.model.childrenDisplay === "submenu"
                                    source: "go-next-symbolic"
                                    color: Appearance.muted
                                    Layout.preferredWidth: 13
                                    Layout.preferredHeight: 13
                                }
                                Item {
                                    Layout.preferredWidth: 4
                                }
                            }
                            onClicked: root.activate(row.index, row.model.label, row.model.childrenDisplay === "submenu", !!row.model.toggleType)
                            onActiveFocusChanged: if (activeFocus)
                                scroller.contentY = Math.max(0, Math.min(scroller.contentHeight - scroller.height, row.y < scroller.contentY ? row.y : Math.max(scroller.contentY, row.y + height + 16 - scroller.height)))
                            Keys.onDownPressed: root.navigate(row.index, 1)
                            Keys.onUpPressed: root.navigate(row.index, -1)
                            Keys.onReturnPressed: clicked()
                            Keys.onEnterPressed: clicked()
                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                                    root.navigate(event.key === Qt.Key_Home ? -1 : 0, event.key === Qt.Key_Home ? 1 : -1);
                                    event.accepted = true;
                                }
                            }
                            Keys.onRightPressed: if (row.model.childrenDisplay === "submenu")
                                clicked()
                        }
                    }
                }
                DetailText {
                    width: parent.width
                    visible: !menuModel.count
                    text: root.failure || (menuModel.valid ? qsTr("No actions available") : qsTr("Loading app menu…"))
                    size: 12
                    muted: true
                    padding: 12
                }
                ShellButton {
                    visible: !!root.failure
                    text: qsTr("Open application menu")
                    flat: true
                    onClicked: {
                        root.nativeMenuRequested(root.entry, root.mapToGlobal(root.width / 2, 0));
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
            Layout.fillWidth: true
            Layout.margins: 8
            Layout.leftMargin: 13
            Layout.rightMargin: 13
            ShellButton {
                flat: true
                iconName: "pin"
                text: root.entry.visibility === "pinned" ? qsTr("Unpin from bar") : qsTr("Pin to bar")
                onClicked: TrayModel.setVisibility(root.entry.preferenceKey, root.entry.visibility === "pinned" ? "auto" : "pinned")
            }
            Item {
                Layout.fillWidth: true
            }
            TrayButton {
                iconName: "preferences-system"
                label: qsTr("Tray settings")
                onClicked: root.settingsRequested()
            }
        }
    }
}
