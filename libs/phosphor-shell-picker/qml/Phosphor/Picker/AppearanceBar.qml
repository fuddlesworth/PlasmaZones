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
    required property var controller
    property var availableWidgets: []
    property string chosen: controller.chosenWidget
    onChosenChanged: controller.chosenWidget = chosen
    property string dragged: ""
    readonly property var layout: Appearance.settings.barLayout
    readonly property var regions: ["left", "center", "right"]
    readonly property var used: regions.reduce((result, region) => result.concat(flattened(region)), [])
    readonly property string region: regions.find(r => flattened(r).includes(chosen)) || ""
    readonly property int chosenIndex: region ? flattened(region).indexOf(chosen) : -1
    readonly property var names: ({
            launcher: qsTr("Launcher"),
            focusedapp: qsTr("Focused app"),
            media: qsTr("Media"),
            placementmap: qsTr("Workspace map"),
            workspaces: qsTr("Workspaces"),
            clock: qsTr("Date & time"),
            notification: qsTr("Notifications"),
            tray: qsTr("System tray"),
            controlcenter: qsTr("Quick settings"),
            appearance: qsTr("Appearance"),
            power: qsTr("Power"),
            systemmetrics: qsTr("System metrics"),
            audio: qsTr("Audio"),
            battery: qsTr("Battery"),
            bluetooth: qsTr("Bluetooth"),
            network: qsTr("Network"),
            spacer: qsTr("Spacer")
        })
    readonly property var icons: ({
            launcher: "view-grid",
            focusedapp: "utilities-terminal",
            media: "audio-x-generic",
            placementmap: "view-grid",
            workspaces: "view-grid",
            clock: "view-calendar",
            notification: "notifications",
            tray: "view-more-symbolic",
            controlcenter: "preferences-system",
            appearance: "preferences-desktop-theme",
            power: "system-shutdown",
            systemmetrics: "utilities-system-monitor",
            audio: "audio-volume-high",
            battery: "battery-full",
            bluetooth: "bluetooth",
            network: "network-wireless",
            spacer: "view-split-left-right"
        })
    readonly property var hiddenWidgets: availableWidgets.filter(id => !used.includes(id))
    implicitHeight: pageLayout.implicitHeight
    function flattened(region) {
        const result = [];
        const groups = layout[region] || [];
        for (let i = 0; i < groups.length; ++i)
            for (let j = 0; j < groups[i].length; ++j)
                result.push(String(groups[i][j]));
        return result;
    }
    function move(region, index) {
        AppearanceStore.moveWidget(chosen, region, index);
    }
    GridLayout {
        id: pageLayout
        columns: root.width < 740 ? 1 : 2
        anchors.left: parent.left
        anchors.right: parent.right
        columnSpacing: 24
        rowSpacing: 24
        ColumnLayout {
            id: editor
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 20
            LookImage {
                Layout.fillWidth: true
                implicitHeight: 180
                path: {
                    const walls = Appearance.settings.wallpapers;
                    return (walls[root.controller.selectedScreen] || walls[""] || {}).path || "";
                }
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: Qt.alpha(Appearance.recess, .4)
                }
                LookText {
                    x: 18
                    y: 15
                    text: qsTr("YOUR BAR, IN PLACE")
                    kicker: true
                }
                Rectangle {
                    x: Appearance.settings.barInset
                    width: parent.width - x * 2
                    y: Appearance.bottom ? 111 : 46
                    height: 39
                    radius: Math.min(12, Appearance.radius)
                    color: Qt.alpha(Appearance.surface, .95)
                    border.color: Appearance.outline
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 16
                        Repeater {
                            model: root.regions
                            RowLayout {
                                required property string modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.alignment: modelData === "left" ? Qt.AlignLeft : modelData === "right" ? Qt.AlignRight : Qt.AlignHCenter
                                spacing: 6
                                Item {
                                    visible: parent.modelData !== "left"
                                    Layout.fillWidth: true
                                }
                                Repeater {
                                    model: root.flattened(parent.modelData)
                                    ShellIcon {
                                        required property string modelData
                                        implicitWidth: 12
                                        implicitHeight: 12
                                        source: root.icons[modelData] || "applications-system"
                                        color: Appearance.text
                                    }
                                }
                                Item {
                                    visible: parent.modelData !== "right"
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
                LookText {
                    x: 18
                    y: Appearance.bottom ? 67 : 139
                    text: qsTr("Drag widgets between regions. Select one for more controls.")
                    size: 9
                    muted: true
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Repeater {
                    model: root.regions
                    LookCard {
                        id: regionCard
                        required property string modelData
                        readonly property var widgets: root.flattened(modelData)
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        implicitHeight: Math.max(250, body.implicitHeight + 32)
                        color: regionDrop.containsDrag ? Qt.alpha(Appearance.accent, .15) : Qt.alpha(Appearance.card, .22)
                        RowLayout {
                            Layout.fillWidth: true
                            LookText {
                                text: regionCard.modelData === "left" ? qsTr("Left") : regionCard.modelData === "center" ? qsTr("Center") : qsTr("Right")
                                size: 12
                                Layout.fillWidth: true
                            }
                            LookText {
                                text: String(regionCard.widgets.length)
                                muted: true
                                size: 9
                            }
                        }
                        Repeater {
                            model: regionCard.widgets
                            Basic.AbstractButton {
                                id: chip
                                required property string modelData
                                required property int index
                                Layout.fillWidth: true
                                implicitHeight: 34
                                Accessible.name: root.names[modelData] || modelData
                                onClicked: root.chosen = modelData
                                onPressed: root.chosen = modelData
                                background: Rectangle {
                                    radius: 7
                                    color: root.chosen === chip.modelData ? Qt.alpha(Appearance.accent, .14) : Qt.alpha(Appearance.card, .4)
                                    border.color: chip.visualFocus ? Appearance.text : root.chosen === chip.modelData ? Appearance.accent : Appearance.outline
                                }
                                contentItem: RowLayout {
                                    spacing: 6
                                    LookText {
                                        text: "⠿"
                                        muted: true
                                        Layout.leftMargin: 6
                                    }
                                    ShellIcon {
                                        source: root.icons[chip.modelData] || "applications-system"
                                        implicitWidth: 13
                                        implicitHeight: 13
                                        color: Appearance.muted
                                    }
                                    LookText {
                                        text: root.names[chip.modelData] || chip.modelData
                                        size: 9
                                        Layout.fillWidth: true
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                    }
                                }
                                DragHandler {
                                    id: drag
                                    target: null
                                    onActiveChanged: {
                                        if (active)
                                            root.dragged = chip.modelData;
                                        else {
                                            ghost.Drag.drop();
                                            root.dragged = "";
                                        }
                                    }
                                }
                                Item {
                                    id: ghost
                                    x: drag.centroid.position.x
                                    y: drag.centroid.position.y
                                    width: 1
                                    height: 1
                                    Drag.active: drag.active
                                    Drag.source: chip
                                    Drag.keys: ["appearance-widget"]
                                }
                                DropArea {
                                    anchors.fill: parent
                                    keys: ["appearance-widget"]
                                    onDropped: drop => {
                                        if (root.dragged && root.dragged !== chip.modelData)
                                            AppearanceStore.moveWidget(root.dragged, regionCard.modelData, chip.index);
                                        drop.acceptProposedAction();
                                    }
                                }
                            }
                        }
                        LookText {
                            visible: !regionCard.widgets.length
                            text: qsTr("Drop a widget here")
                            muted: true
                            size: 9
                            Layout.fillWidth: true
                        }
                        DropArea {
                            id: regionDrop
                            parent: regionCard
                            anchors.fill: parent
                            z: -1
                            keys: ["appearance-widget"]
                            onDropped: drop => {
                                if (root.dragged)
                                    AppearanceStore.moveWidget(root.dragged, regionCard.modelData);
                                drop.acceptProposedAction();
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                LookText {
                    text: qsTr("Available widgets")
                    size: 13
                }
                LookText {
                    text: qsTr("Hidden widgets stay here. Add them whenever you like.")
                    muted: true
                    size: 10
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: root.hiddenWidgets
                        ShellButton {
                            required property string modelData
                            text: (root.names[modelData] || modelData) + "  +"
                            iconName: root.icons[modelData] || "applications-system"
                            onClicked: {
                                root.chosen = modelData;
                                AppearanceStore.moveWidget(modelData, "right");
                            }
                        }
                    }
                }
                LookText {
                    visible: !root.hiddenWidgets.length
                    text: qsTr("All widgets are in your bar.")
                    muted: true
                    size: 10
                }
            }
        }
        LookCard {
            id: inspector
            Layout.preferredWidth: pageLayout.columns === 1 ? -1 : 236
            Layout.fillWidth: pageLayout.columns === 1
            Layout.alignment: Qt.AlignTop
            LookText {
                text: qsTr("Bar placement")
                size: 14
            }
            LookField {
                Layout.fillWidth: true
                label: qsTr("Screen edge")
                setting: "edge"
                choices: [
                    {
                        value: "top",
                        label: qsTr("Top")
                    },
                    {
                        value: "bottom",
                        label: qsTr("Bottom")
                    }
                ]
            }
            LookRange {
                Layout.fillWidth: true
                label: qsTr("Screen inset")
                setting: "barInset"
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Appearance.outline
            }
            ColumnLayout {
                spacing: 5
                LookText {
                    text: root.names[root.chosen] || root.chosen
                    size: 14
                }
                LookText {
                    text: root.region ? qsTr("Selected widget") : qsTr("This widget is hidden.")
                    size: 9
                    muted: true
                }
            }
            LookField {
                Layout.fillWidth: true
                visible: !!root.region
                label: qsTr("Region")
                value: root.region
                choices: [
                    {
                        value: "left",
                        label: qsTr("Left")
                    },
                    {
                        value: "center",
                        label: qsTr("Center")
                    },
                    {
                        value: "right",
                        label: qsTr("Right")
                    }
                ]
                onChosen: value => root.move(value, -1)
            }
            LookText {
                text: qsTr("ORDER WITHIN REGION")
                kicker: true
                visible: !!root.region
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: !!root.region
                spacing: 8
                ShellButton {
                    Layout.fillWidth: true
                    text: qsTr("← Move earlier")
                    enabled: root.chosenIndex > 0
                    onClicked: root.move(root.region, root.chosenIndex - 1)
                }
                ShellButton {
                    Layout.fillWidth: true
                    text: qsTr("Move later →")
                    enabled: root.region && root.chosenIndex < root.flattened(root.region).length - 1
                    onClicked: root.move(root.region, root.chosenIndex + 1)
                }
                ShellButton {
                    text: qsTr("Hide widget")
                    flat: true
                    onClicked: root.move("", -1)
                }
            }
            ShellButton {
                visible: !root.region
                text: qsTr("Add to bar")
                onClicked: root.move("right", -1)
            }
            LookToggle {
                Layout.fillWidth: true
                visible: root.chosen === "media"
                setting: "media"
                label: qsTr("Show media")
                description: qsTr("Display the current track in the bar.")
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Appearance.outline
            }
            LookText {
                Layout.fillWidth: true
                text: qsTr("The workspace map stays readable as more windows open. Long labels make room for the controls.")
                muted: true
                size: 10
                lineHeight: 1.5
            }
            ShellButton {
                text: qsTr("Restore default layout")
                flat: true
                labelSize: 10
                onClicked: AppearanceStore.resetBarLayout()
            }
        }
    }
}
