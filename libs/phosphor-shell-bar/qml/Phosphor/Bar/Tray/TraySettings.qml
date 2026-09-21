// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

ColumnLayout {
    id: root
    objectName: "traySettingsPage"
    property var shownItems: TrayModel.barItems
    property string query: ""
    readonly property var filtered: TrayModel.items.filter(entry => TrayModel.title(entry).toLocaleLowerCase().includes(query.toLocaleLowerCase()))
    spacing: 15
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 54
        color: Qt.alpha(Appearance.recess, 0.55)
        border.color: Appearance.outline
        radius: Appearance.radius * 0.65
        RowLayout {
            anchors.fill: parent
            anchors.margins: 15
            DetailText {
                text: qsTr("IN YOUR BAR")
                size: 9
                muted: true
                font.letterSpacing: 1.6
                Layout.fillWidth: true
            }
            Repeater {
                model: root.shownItems
                TrayIcon {
                    required property var modelData
                    entry: modelData
                    Layout.preferredWidth: 21
                    Layout.preferredHeight: 21
                }
            }
            ShellIcon {
                source: "go-up-symbolic"
                color: Appearance.muted
                Layout.preferredWidth: 13
                Layout.preferredHeight: 13
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        DetailText {
            text: qsTr("Icon appearance")
            size: 13
            Layout.fillWidth: true
        }
        ShellComboBox {
            objectName: "trayIconStyle"
            Layout.preferredWidth: 145
            model: [qsTr("Shell tint"), qsTr("App colors")]
            currentIndex: Appearance.settings.trayIcons === "color" ? 1 : 0
            onActivated: AppearanceStore.setValue("trayIcons", currentIndex ? "color" : "symbolic")
            Accessible.name: qsTr("Icon appearance")
        }
    }
    RowLayout {
        Layout.fillWidth: true
        DetailText {
            text: qsTr("Maximum bar icons")
            size: 13
            Layout.fillWidth: true
        }
        ShellComboBox {
            objectName: "trayLimit"
            Layout.preferredWidth: 145
            model: [qsTr("Overflow only"), qsTr("1 icon"), qsTr("2 icons"), qsTr("3 icons"), qsTr("4 icons")]
            currentIndex: Appearance.settings.trayLimit
            onActivated: AppearanceStore.setValue("trayLimit", currentIndex)
            Accessible.name: qsTr("Maximum bar icons")
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 16
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5
            DetailText {
                Layout.fillWidth: true
                text: qsTr("Surface important activity")
                size: 13
            }
            DetailText {
                Layout.fillWidth: true
                text: qsTr("Temporarily show apps that need attention. Always-hidden apps stay hidden.")
                size: 11
                muted: true
            }
        }
        DetailSwitch {
            objectName: "trayAttention"
            on: Appearance.settings.trayAttention
            Accessible.name: qsTr("Surface important activity")
            onClicked: AppearanceStore.setValue("trayAttention", !on)
        }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Appearance.outline
    }
    RowLayout {
        Layout.fillWidth: true
        DetailText {
            text: qsTr("Your apps")
            size: 14
            Layout.fillWidth: true
        }
        DetailText {
            text: qsTr("Drag to reorder")
            size: 10
            muted: true
        }
    }
    Basic.TextField {
        id: search
        objectName: "traySearch"
        Layout.fillWidth: true
        implicitHeight: 33
        placeholderText: qsTr("Find an app")
        Accessible.name: qsTr("Find tray app")
        onTextEdited: root.query = text
        color: Appearance.text
        placeholderTextColor: Appearance.muted
        font.family: Tokens.font_family_ui
        font.pixelSize: Math.round(12 * Appearance.textScale)
        background: Rectangle {
            radius: 7
            color: Appearance.recess
            border.width: 1
            border.color: search.activeFocus ? Appearance.text : Appearance.outline
        }
    }
    Column {
        Layout.fillWidth: true
        Repeater {
            id: appRows
            model: root.filtered
            delegate: Item {
                id: appRow
                required property var modelData
                width: parent.width
                height: 65
                readonly property int position: TrayModel.items.findIndex(entry => entry.instanceKey === modelData.instanceKey)
                function focusPolicy(): void {
                    policy.forceActiveFocus();
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Appearance.outline
                }
                RowLayout {
                    anchors.fill: parent
                    spacing: 9
                    Item {
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 32
                        DetailText {
                            anchors.centerIn: parent
                            text: "⠿"
                            muted: true
                            size: 17
                            Accessible.ignored: true
                        }
                        MouseArea {
                            id: grip
                            anchors.fill: parent
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            drag.target: dragToken
                            onReleased: {
                                dragToken.Drag.drop();
                                dragToken.x = 0;
                                dragToken.y = 0;
                            }
                            onCanceled: {
                                dragToken.Drag.cancel();
                                dragToken.x = 0;
                                dragToken.y = 0;
                            }
                        }
                        Item {
                            id: dragToken
                            width: 14
                            height: 32
                            property string preferenceKey: appRow.modelData.preferenceKey
                            Drag.active: grip.drag.active
                            Drag.source: dragToken
                            Drag.hotSpot.x: 7
                            Drag.hotSpot.y: 16
                            Drag.keys: ["phosphor-tray-app"]
                        }
                    }
                    TrayIcon {
                        entry: appRow.modelData
                        Layout.preferredWidth: 19
                        Layout.preferredHeight: 19
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        DetailText {
                            Layout.fillWidth: true
                            text: TrayModel.title(appRow.modelData)
                            size: 12
                            maximumLineCount: 1
                            elide: Text.ElideRight
                        }
                        DetailText {
                            Layout.fillWidth: true
                            text: TrayModel.status(appRow.modelData)
                            size: 9
                            muted: true
                            maximumLineCount: 1
                            elide: Text.ElideRight
                        }
                    }
                    ShellComboBox {
                        id: policy
                        objectName: "trayVisibility-" + appRow.modelData.preferenceKey
                        Layout.preferredWidth: 108
                        labelSize: 10
                        readonly property var policies: ["pinned", "auto", "overflow", "hidden"]
                        model: [qsTr("Pin to bar"), qsTr("Automatic"), qsTr("Overflow only"), qsTr("Always hide")]
                        currentIndex: policies.indexOf(appRow.modelData.visibility)
                        Accessible.name: qsTr("%1 visibility").arg(TrayModel.title(appRow.modelData))
                        onActivated: TrayModel.setVisibility(appRow.modelData.preferenceKey, policies[currentIndex])
                    }
                    Column {
                        TrayButton {
                            width: 19
                            height: 19
                            iconName: "go-up-symbolic"
                            label: qsTr("Move %1 earlier").arg(TrayModel.title(appRow.modelData))
                            enabled: appRow.position > 0
                            onClicked: root.reorder(appRow.modelData.preferenceKey, -1)
                        }
                        TrayButton {
                            width: 19
                            height: 19
                            iconName: "go-down-symbolic"
                            label: qsTr("Move %1 later").arg(TrayModel.title(appRow.modelData))
                            enabled: appRow.position < TrayModel.items.length - 1
                            onClicked: root.reorder(appRow.modelData.preferenceKey, 1)
                        }
                    }
                }
                DropArea {
                    anchors.fill: parent
                    keys: ["phosphor-tray-app"]
                    onDropped: drop => {
                        if (drop.source) {
                            const key = drop.source.preferenceKey, before = appRow.modelData.preferenceKey;
                            Qt.callLater(() => TrayModel.move(key, before));
                            drop.acceptProposedAction();
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: parent.containsDrag
                        color: Qt.alpha(Appearance.accent, 0.12)
                        radius: 6
                    }
                }
            }
        }
    }
    DetailText {
        Layout.fillWidth: true
        visible: !root.filtered.length
        text: root.query ? qsTr("No matching apps.") : qsTr("Apps will appear here when they register a tray icon.")
        muted: true
        size: 12
    }
    DetailText {
        Layout.fillWidth: true
        text: qsTr("Pinned apps fill the bar first. Automatic apps stay in overflow until they need you. Icons that don’t fit stay in overflow.")
        muted: true
        size: 11
    }
    function reorder(key: string, direction: int): void {
        TrayModel.step(key, direction);
        Qt.callLater(() => {
            for (let i = 0; i < appRows.count; ++i) {
                const row = appRows.itemAt(i);
                if (row.modelData.preferenceKey === key) {
                    row.focusPolicy();
                    break;
                }
            }
        });
    }
}
