// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Column {
    id: root
    spacing: 18
    function toggle(metric: string): void {
        const selected = StatsModel.metrics.slice();
        const index = selected.indexOf(metric);
        if (index >= 0 && selected.length > 1)
            selected.splice(index, 1);
        else if (index < 0 && selected.length < 3)
            selected.push(metric);
        AppearanceStore.setValue("statsMetrics", selected);
    }
    DetailText {
        width: parent.width
        text: qsTr("Your desktop, your pulse.")
        size: 22
        font.letterSpacing: -0.5
    }
    DetailText {
        width: parent.width
        text: qsTr("Keep the readings you care about within reach.")
        size: 11
        muted: true
    }
    Rectangle {
        width: parent.width
        height: 60
        radius: Appearance.radius * 0.65
        color: Qt.alpha(Appearance.recess, 0.5)
        border.width: 1
        border.color: Appearance.outline
        RowLayout {
            anchors.fill: parent
            anchors.margins: 17
            DetailText {
                text: qsTr("BAR PREVIEW")
                muted: true
                size: 8
                font.letterSpacing: 1.6
                Layout.fillWidth: true
            }
            StatsReadout {
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }
    Column {
        width: parent.width
        spacing: 10
        DetailText {
            text: qsTr("Readout style")
            size: 12
        }
        RowLayout {
            width: parent.width
            spacing: 8
            Repeater {
                model: [
                    {
                        id: "traces",
                        name: qsTr("Traces")
                    },
                    {
                        id: "meters",
                        name: qsTr("Meters")
                    },
                    {
                        id: "numbers",
                        name: qsTr("Numbers")
                    }
                ]
                Basic.AbstractButton {
                    id: styleButton
                    required property var modelData
                    objectName: "statsStyle_" + modelData.id
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 74
                    Accessible.name: modelData.name
                    Accessible.role: Accessible.RadioButton
                    Accessible.checked: Appearance.settings.statsStyle === modelData.id
                    onClicked: AppearanceStore.setValue("statsStyle", modelData.id)
                    background: Rectangle {
                        radius: Appearance.radius * 0.6
                        color: Qt.alpha(Appearance.card, 0.6)
                        border.width: 1
                        border.color: styleButton.visualFocus ? Appearance.text : Appearance.settings.statsStyle === styleButton.modelData.id ? Appearance.stops[1] : Appearance.outline
                    }
                    contentItem: Column {
                        spacing: 9
                        StatsReadout {
                            anchors.horizontalCenter: parent.horizontalCenter
                            metrics: ["cpu"]
                            style: styleButton.modelData.id
                            height: 25
                        }
                        DetailText {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: styleButton.modelData.name
                            size: 10
                        }
                    }
                    topPadding: 13
                    bottomPadding: 12
                }
            }
        }
    }
    Column {
        width: parent.width
        spacing: 11
        RowLayout {
            width: parent.width
            DetailText {
                Layout.fillWidth: true
                text: qsTr("Readings in the bar")
                size: 12
            }
            DetailText {
                text: qsTr("%1 / 3").arg(StatsModel.metrics.length)
                muted: true
                size: 10
            }
        }
        DetailText {
            width: parent.width
            text: qsTr("Choose up to three to keep the bar compact.")
            muted: true
            size: 10
        }
        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 7
            rowSpacing: 7
            Repeater {
                model: ["cpu", "gpu", "memory", "network", "storage"]
                Basic.AbstractButton {
                    id: metricButton
                    required property string modelData
                    objectName: "statsMetric_" + modelData
                    readonly property bool selected: StatsModel.metrics.includes(modelData)
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.columnSpan: modelData === "storage" ? 2 : 1
                    implicitHeight: 43
                    enabled: selected ? StatsModel.metrics.length > 1 : StatsModel.metrics.length < 3
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: modelData === "storage" ? qsTr("System disk") : StatsModel.label(modelData)
                    Accessible.checked: selected
                    opacity: enabled || selected ? 1 : 0.45
                    onClicked: root.toggle(modelData)
                    background: Rectangle {
                        radius: Appearance.radius * 0.45
                        color: Qt.alpha(Appearance.recess, 0.25)
                        border.width: 1
                        border.color: metricButton.visualFocus ? Appearance.text : Appearance.outline
                    }
                    contentItem: RowLayout {
                        spacing: 9
                        ShellIcon {
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            source: StatsModel.icon(metricButton.modelData)
                            isMask: true
                            color: Appearance.stops[StatsModel.tone(metricButton.modelData)]
                        }
                        DetailText {
                            Layout.fillWidth: true
                            text: metricButton.Accessible.name
                            size: 11
                        }
                        Rectangle {
                            implicitWidth: 14
                            implicitHeight: 14
                            radius: 2
                            color: metricButton.selected ? Appearance.stops[1] : "transparent"
                            border.width: 1
                            border.color: Appearance.outline
                            ShellIcon {
                                anchors.fill: parent
                                anchors.margins: 1
                                source: "checkmark"
                                color: Appearance.recess
                                isMask: true
                                visible: metricButton.selected
                            }
                        }
                    }
                    leftPadding: 12
                    rightPadding: 12
                }
            }
        }
    }
    RowLayout {
        width: parent.width
        DetailText {
            Layout.fillWidth: true
            text: qsTr("Memory readout")
            size: 11
        }
        ShellComboBox {
            objectName: "statsMemoryUnit"
            Layout.preferredWidth: 140
            model: [qsTr("Percent used"), qsTr("GiB used")]
            currentIndex: Appearance.settings.statsMemoryUnit === "used" ? 1 : 0
            Accessible.name: qsTr("Memory readout")
            onActivated: AppearanceStore.setValue("statsMemoryUnit", currentIndex === 1 ? "used" : "percent")
        }
    }
    RowLayout {
        width: parent.width
        DetailText {
            Layout.fillWidth: true
            text: qsTr("Refresh every")
            size: 11
        }
        ShellComboBox {
            objectName: "statsRefreshInterval"
            Layout.preferredWidth: 140
            model: [qsTr("1 second"), qsTr("2 seconds"), qsTr("5 seconds")]
            currentIndex: [1, 2, 5].indexOf(Appearance.settings.statsInterval)
            Accessible.name: qsTr("Refresh interval")
            onActivated: AppearanceStore.setValue("statsInterval", [1, 2, 5][currentIndex])
        }
    }
    DetailText {
        width: parent.width
        text: qsTr("Colors follow your shell palette. These choices are saved with your bar preset.")
        size: 10
        muted: true
        lineHeight: 1.4
    }
    ShellButton {
        text: qsTr("Reset widget")
        iconName: "view-refresh"
        flat: true
        foreground: Appearance.muted
        onClicked: {
            const next = Object.assign({}, Appearance.settings, {
                statsStyle: "traces",
                statsMetrics: ["cpu", "gpu", "memory"],
                statsMemoryUnit: "percent",
                statsInterval: 2,
                statsGpuId: ""
            });
            AppearanceStore.setValues(next);
        }
    }
    DetailNotice {
        text: AppearanceStore.error
        error: true
    }
}
