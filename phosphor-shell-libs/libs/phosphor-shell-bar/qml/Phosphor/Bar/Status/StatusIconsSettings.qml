// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

ColumnLayout {
    id: root
    required property var controller
    signal pageRequested(string page)
    signal detailRequested(string panelId)
    spacing: Tokens.spacing_l
    function focusFirst(): void {
        limit.forceActiveFocus();
    }
    function focusPolicy(id: string): void {
        for (let index = 0; index < rows.count; ++index) {
            const row = rows.itemAt(index);
            if (row.modelData.id === id) {
                row.focusPolicy();
                return;
            }
        }
    }
    function move(id: string, delta: int): void {
        controller.moveIcon(id, delta);
        Qt.callLater(() => root.focusPolicy(id));
    }
    function moveBefore(id: string, beforeId: string): void {
        controller.moveBefore(id, beforeId);
        Qt.callLater(() => root.focusPolicy(id));
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: preview.implicitHeight + Tokens.spacing_l * 2
        radius: Tokens.radius_m
        color: Qt.alpha(Appearance.recess, 0.6)
        border.width: 1
        border.color: Appearance.outline
        RowLayout {
            id: preview
            x: Tokens.spacing_l
            y: Tokens.spacing_l
            width: parent.width - Tokens.spacing_l * 2
            spacing: Tokens.spacing_m
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.spacing_xs
                DetailText {
                    text: i18n("IN YOUR BAR")
                    muted: true
                    font.pixelSize: Tokens.font_size_label_s
                }
                DetailText {
                    Layout.fillWidth: true
                    text: i18n("Updates as you arrange")
                    muted: true
                    font.pixelSize: Tokens.font_size_label_s
                }
            }
            ControlCenterButton {
                controller: root.controller
                onActivated: {
                    if (requestedStatusPage)
                        root.pageRequested(requestedStatusPage);
                    else
                        root.detailRequested("");
                }
            }
        }
    }
    DetailNotice {
        objectName: "statusSettingsError"
        Layout.fillWidth: true
        text: root.controller ? root.controller.error || "" : ""
        error: true
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Tokens.spacing_m
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Tokens.spacing_xs
            DetailText {
                text: i18n("Visible icons")
                font.pixelSize: Tokens.font_size_body_s
            }
            DetailText {
                Layout.fillWidth: true
                text: i18n("Extra icons stay in the more menu.")
                muted: true
                font.pixelSize: Tokens.font_size_label_s
            }
        }
        ShellComboBox {
            id: limit
            objectName: "statusLimit"
            model: [i18n("Up to 2"), i18n("Up to 3"), i18n("Up to 4"), i18n("Up to 5"), i18n("Up to 6")]
            currentIndex: root.controller ? root.controller.maxVisible - 2 : 2
            enabled: root.controller !== null
            Accessible.name: i18n("Maximum visible status icons")
            onActivated: root.controller.setMaxVisible(currentIndex + 2)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Tokens.spacing_m
        DetailText {
            Layout.fillWidth: true
            text: i18n("Battery percentage")
            font.pixelSize: Tokens.font_size_body_s
        }
        DetailSwitch {
            objectName: "statusBatteryPercentage"
            on: root.controller && root.controller.showBatteryPercentage
            enabled: root.controller !== null
            Accessible.name: i18n("Battery percentage")
            onClicked: root.controller.setShowBatteryPercentage(!on)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        DetailText {
            Layout.fillWidth: true
            text: i18n("Your indicators")
            font.pixelSize: Tokens.font_size_body_m
            font.weight: Font.Medium
        }
        DetailText {
            text: i18n("Drag to reorder")
            muted: true
            font.pixelSize: Tokens.font_size_label_s
        }
    }
    Column {
        Layout.fillWidth: true
        Repeater {
            id: rows
            model: root.controller ? root.controller.icons : []
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                width: parent.width
                implicitHeight: Math.max(62, controls.implicitHeight + Tokens.spacing_s * 2)
                height: implicitHeight
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
                    id: controls
                    anchors.fill: parent
                    spacing: Tokens.spacing_s
                    Item {
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 32
                        DetailText {
                            anchors.centerIn: parent
                            text: "⠿"
                            muted: true
                            font.pixelSize: Tokens.font_size_title_l
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
                            property string iconId: row.modelData.id
                            Drag.active: grip.drag.active
                            Drag.source: dragToken
                            Drag.hotSpot.x: width / 2
                            Drag.hotSpot.y: height / 2
                            Drag.keys: ["phosphor-status-icon"]
                        }
                    }
                    StatusIcon {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        entry: row.modelData
                        tint: Appearance.muted
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Tokens.spacing_xs
                        DetailText {
                            Layout.fillWidth: true
                            text: row.modelData.title
                            font.pixelSize: Tokens.font_size_body_s
                        }
                        DetailText {
                            Layout.fillWidth: true
                            text: row.modelData.description
                            muted: true
                            font.pixelSize: Tokens.font_size_label_s
                        }
                    }
                    ShellComboBox {
                        id: policy
                        objectName: "statusVisibility-" + row.modelData.id
                        Layout.preferredWidth: Math.min(132, root.width * 0.3)
                        readonly property var policies: ["always", "auto", "hidden"]
                        model: [i18n("Always"), row.modelData.condition, i18n("Hidden")]
                        currentIndex: policies.indexOf(row.modelData.visibility)
                        Accessible.name: i18n("%1 visibility").arg(row.modelData.title)
                        onActivated: {
                            const id = row.modelData.id;
                            root.controller.setVisibility(id, policies[currentIndex]);
                            Qt.callLater(() => root.focusPolicy(id));
                        }
                    }
                    Column {
                        ShellButton {
                            objectName: "statusEarlier-" + row.modelData.id
                            implicitWidth: 26
                            implicitHeight: 26
                            iconName: "go-up-symbolic"
                            label: i18n("Move %1 earlier").arg(row.modelData.title)
                            flat: true
                            enabled: row.index > 0
                            onClicked: root.move(row.modelData.id, -1)
                        }
                        ShellButton {
                            objectName: "statusLater-" + row.modelData.id
                            implicitWidth: 26
                            implicitHeight: 26
                            iconName: "go-down-symbolic"
                            label: i18n("Move %1 later").arg(row.modelData.title)
                            flat: true
                            enabled: row.index + 1 < rows.count
                            onClicked: root.move(row.modelData.id, 1)
                        }
                    }
                }
                DropArea {
                    anchors.fill: parent
                    keys: ["phosphor-status-icon"]
                    onDropped: drop => {
                        if (!drop.source)
                            return;
                        const source = drop.source.iconId;
                        const before = row.modelData.id;
                        Qt.callLater(() => root.moveBefore(source, before));
                        drop.acceptProposedAction();
                    }
                }
            }
        }
    }
    DetailText {
        Layout.fillWidth: true
        text: i18n("Left-click opens Quick settings. Right-click an icon for its controls, or focus it and press Shift+F10.")
        muted: true
        font.pixelSize: Tokens.font_size_label_s
        lineHeight: 1.4
    }
}
