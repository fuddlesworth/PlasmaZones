// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property var availableWidgets: []
    property real railT: 0.5
    property var sessionState: null
    property bool ready: false
    Component.onCompleted: Qt.callLater(() => {
        if (sessionState) {
            editingLayout = sessionState.editingLayout;
            advanced = sessionState.advanced;
            viewport.contentY = Math.max(0, Math.min(sessionState.scrollOffset, viewport.contentHeight - viewport.height));
        }
        ready = true;
    })
    onEditingLayoutChanged: if (ready && sessionState)
        sessionState.editingLayout = editingLayout
    onAdvancedChanged: if (ready && sessionState)
        sessionState.advanced = advanced
    property bool editingLayout: false
    property bool advanced: false
    readonly property string title: qsTr("Appearance")
    function revealFocusedControl() {
        if (!ready || !Window.window)
            return;
        const item = Window.window.activeFocusItem;
        let ancestor = item;
        while (ancestor && ancestor !== body)
            ancestor = ancestor.parent;
        if (!ancestor)
            return;
        const point = item.mapToItem(viewport.contentItem, 0, 0);
        if (point.y < viewport.contentY)
            viewport.contentY = Math.max(0, point.y);
        else if (point.y + item.height > viewport.contentY + viewport.height)
            viewport.contentY = Math.min(viewport.contentHeight - viewport.height, point.y + item.height - viewport.height);
    }
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() {
            root.revealFocusedControl();
        }
    }
    signal closeRequested
    implicitWidth: editingLayout ? 400 : 285
    implicitHeight: Math.min(Screen.height - 104, body.implicitHeight + 42)
    ShellSurface {
        anchors.fill: parent
    }
    Flickable {
        id: viewport
        objectName: "appearanceScroll"
        onContentYChanged: if (root.ready && root.sessionState)
            root.sessionState.scrollOffset = contentY
        anchors.fill: parent
        anchors.margins: 21
        clip: true
        contentWidth: width
        contentHeight: body.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            x: parent.width + 5
            width: 5
        }
        ColumnLayout {
            id: body
            width: parent.width
            spacing: 16
            Item {
                Layout.fillWidth: true
                implicitHeight: heading.implicitHeight
                Column {
                    id: heading
                    spacing: 8
                    Text {
                        text: qsTr("MAKE IT YOURS")
                        color: Appearance.muted
                        font.pixelSize: 9
                        font.letterSpacing: 1.5
                    }
                    Text {
                        text: qsTr("Same Phosphor.\nYour expression.")
                        color: Appearance.text
                        font.family: Tokens.font_family_ui
                        font.pixelSize: 18
                        font.weight: Font.Medium
                        lineHeightMode: Text.FixedHeight
                        lineHeight: 27
                    }
                }
                ShellButton {
                    objectName: "closeAppearance"
                    anchors.right: parent.right
                    anchors.top: parent.top
                    implicitWidth: 26
                    implicitHeight: 26
                    flat: true
                    iconName: "window-close"
                    Accessible.name: qsTr("Close appearance")
                    onClicked: root.closeRequested()
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Changes apply to both views. Your choices stay when you switch between them.")
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                lineHeightMode: Text.FixedHeight
                lineHeight: 18
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ShellButton {
                    text: qsTr("Style")
                    Layout.fillWidth: true
                    highlighted: !root.editingLayout
                    onClicked: root.editingLayout = false
                }
                ShellButton {
                    text: qsTr("Widgets")
                    Layout.fillWidth: true
                    highlighted: root.editingLayout
                    onClicked: root.editingLayout = true
                }
            }
            ColumnLayout {
                visible: !root.editingLayout
                Layout.fillWidth: true
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    FieldLabel {
                        text: qsTr("Starting point")
                    }
                    Choice {
                        Layout.fillWidth: true
                        model: [qsTr("Custom"), qsTr("Phosphor / deep glass"), qsTr("Paper / bright & quiet"), qsTr("Ember / warm & compact")]
                        currentIndex: ["custom", "phosphor", "paper", "ember"].indexOf(AppearanceStore.currentPreset)
                        Accessible.name: qsTr("Starting point")
                        onActivated: {
                            if (currentIndex > 0)
                                AppearanceStore.applyPreset(["", "phosphor", "paper", "ember"][currentIndex]);
                        }
                    }
                }
                Repeater {
                    model: [
                        {
                            key: "presentation",
                            label: qsTr("Workspace view"),
                            names: [qsTr("Navigator"), qsTr("Stage")],
                            values: ["navigator", "stage"]
                        },
                        {
                            key: "palette",
                            label: qsTr("Color field"),
                            names: [qsTr("Phosphor spectrum"), qsTr("Wallpaper / iris to peach"), qsTr("Ember / honey to rust")],
                            values: ["spectrum", "wallpaper", "ember"]
                        },
                        {
                            key: "material",
                            label: qsTr("Surface material"),
                            names: [qsTr("Tinted glass"), qsTr("Opaque"), qsTr("Light ceramic")],
                            values: ["glass", "solid", "light"]
                        },
                        {
                            key: "edge",
                            label: qsTr("Bar placement"),
                            names: [qsTr("Top"), qsTr("Bottom")],
                            values: ["top", "bottom"]
                        },
                        {
                            key: "density",
                            label: qsTr("Density"),
                            names: [qsTr("Comfortable"), qsTr("Compact")],
                            values: ["comfortable", "compact"]
                        }
                    ]
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        FieldLabel {
                            text: parent.modelData.label
                        }
                        Choice {
                            id: choice
                            objectName: "appearanceChoice-" + parent.modelData.key
                            Layout.fillWidth: true
                            model: parent.modelData.names
                            currentIndex: parent.modelData.values.indexOf(Appearance.settings[parent.modelData.key])
                            Accessible.name: parent.modelData.label
                            onActivated: AppearanceStore.setValue(parent.modelData.key, parent.modelData.values[currentIndex])
                        }
                    }
                }
                Repeater {
                    model: [
                        {
                            key: "radius",
                            label: qsTr("Corner radius"),
                            minimum: 4
                        },
                        {
                            key: "gap",
                            label: qsTr("Window gap"),
                            minimum: 6
                        }
                    ]
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            FieldLabel {
                                Layout.fillWidth: true
                                text: parent.parent.modelData.label
                            }
                            Text {
                                text: qsTr("%1 px").arg(Math.round(slider.value))
                                color: Appearance.muted
                                font.family: Tokens.font_family_mono
                                font.pixelSize: 10
                            }
                        }
                        Basic.Slider {
                            id: slider
                            Layout.fillWidth: true
                            implicitHeight: 16
                            from: parent.modelData.minimum
                            to: 30
                            stepSize: 1
                            value: Appearance.settings[parent.modelData.key]
                            Accessible.name: parent.modelData.label
                            onMoved: if (!pressed)
                                AppearanceStore.setValue(parent.modelData.key, Math.round(value))
                            onPressedChanged: if (!pressed)
                                AppearanceStore.setValue(parent.modelData.key, Math.round(value))
                            background: Rectangle {
                                x: slider.leftPadding
                                y: (slider.height - height) / 2
                                width: slider.availableWidth
                                height: 4
                                radius: 2
                                color: Appearance.recess
                                Rectangle {
                                    width: parent.width * slider.visualPosition
                                    height: parent.height
                                    radius: 2
                                    color: Appearance.accent
                                }
                            }
                            handle: Rectangle {
                                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                                y: (slider.height - height) / 2
                                width: 12
                                height: 12
                                radius: 6
                                color: Appearance.text
                                border.width: slider.visualFocus ? 2 : 0
                                border.color: Appearance.accent
                            }
                        }
                    }
                }
                SettingCheck {
                    label: qsTr("Edge glow")
                    settingKey: "glow"
                }
                SettingCheck {
                    label: qsTr("Media widget in bar")
                    settingKey: "media"
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    FieldLabel {
                        text: qsTr("Media visualizer")
                    }
                    Choice {
                        Layout.fillWidth: true
                        model: [qsTr("Ribbon"), qsTr("Bars"), qsTr("Halo"), qsTr("Off")]
                        currentIndex: ["ribbon", "bars", "halo", "off"].indexOf(Appearance.visualizer)
                        Accessible.name: qsTr("Media visualizer")
                        onActivated: AppearanceStore.setValue("visualizer", ["ribbon", "bars", "halo", "off"][currentIndex])
                    }
                }
                SettingCheck {
                    label: qsTr("Motion")
                    settingKey: "motion"
                }
                ShellButton {
                    text: root.advanced ? qsTr("Fewer options") : qsTr("More options")
                    Layout.fillWidth: true
                    onClicked: root.advanced = !root.advanced
                }
                ColumnLayout {
                    visible: root.advanced
                    Layout.fillWidth: true
                    spacing: 16
                    SettingCheck {
                        label: qsTr("Match desktop windows")
                        settingKey: "desktopStyle"
                    }
                    SettingCheck {
                        label: qsTr("Custom surface packs")
                        settingKey: "surfacePacks"
                    }
                    Repeater {
                        model: [
                            {
                                key: "uiFont",
                                label: qsTr("Interface font")
                            },
                            {
                                key: "monoFont",
                                label: qsTr("Number font")
                            }
                        ]
                        ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            FieldLabel {
                                text: parent.modelData.label
                            }
                            Basic.TextField {
                                id: fontField
                                Layout.fillWidth: true
                                implicitHeight: 34
                                text: Appearance.settings[parent.modelData.key]
                                placeholderText: qsTr("Default font")
                                color: Appearance.text
                                placeholderTextColor: Appearance.muted
                                selectionColor: Appearance.accent
                                selectedTextColor: Appearance.text
                                font.family: Tokens.font_family_ui
                                font.pixelSize: 11
                                maximumLength: 80
                                Accessible.name: parent.modelData.label
                                onEditingFinished: AppearanceStore.setValue(parent.modelData.key, text.trim())
                                background: Rectangle {
                                    radius: 6
                                    color: Appearance.recess
                                    border.width: 1
                                    border.color: fontField.activeFocus ? Appearance.accent : Appearance.outline
                                }
                            }
                        }
                    }
                }
            }
            BarLayoutEditor {
                visible: root.editingLayout
                Layout.fillWidth: true
                availableWidgets: root.availableWidgets
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ShellButton {
                    text: qsTr("Export preset ↓")
                    Layout.fillWidth: true
                    onClicked: exportDialog.open()
                }
                ShellButton {
                    text: qsTr("Reset")
                    onClicked: AppearanceStore.applyPreset("phosphor")
                }
            }
            ShellButton {
                text: qsTr("Import preset")
                Layout.fillWidth: true
                onClicked: importDialog.open()
            }
            Text {
                Layout.fillWidth: true
                visible: AppearanceStore.error !== ""
                text: AppearanceStore.error
                color: Appearance.at(1)
                wrapMode: Text.WordWrap
                font.pixelSize: 11
            }
        }
    }
    FileDialog {
        id: importDialog
        title: qsTr("Import appearance")
        nameFilters: [qsTr("JSON presets (*.json)")]
        onAccepted: AppearanceStore.importPreset(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export appearance")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("JSON presets (*.json)")]
        onAccepted: AppearanceStore.exportPreset(selectedFile)
    }
    component FieldLabel: Text {
        color: Appearance.muted
        font.family: Tokens.font_family_ui
        font.pixelSize: 11
    }
    component Choice: ShellComboBox {
        implicitHeight: 34
        background: Rectangle {
            radius: 6
            color: Appearance.recess
            border.width: 1
            border.color: parent.visualFocus ? Appearance.text : Appearance.outline
        }
    }
    component SettingCheck: Basic.CheckBox {
        id: check
        property string label
        property string settingKey
        Layout.fillWidth: true
        implicitHeight: 20
        spacing: 9
        checked: Appearance.settings[settingKey]
        text: label
        onToggled: AppearanceStore.setValue(settingKey, checked)
        indicator: Rectangle {
            x: check.leftPadding
            y: (check.height - height) / 2
            width: 13
            height: 13
            radius: 3
            color: check.checked ? Appearance.accent : Appearance.recess
            border.width: 1
            border.color: check.visualFocus ? Appearance.text : Appearance.outline
            Text {
                anchors.centerIn: parent
                text: "✓"
                visible: check.checked
                color: Appearance.recess
                font.pixelSize: 11
            }
        }
        contentItem: Text {
            leftPadding: check.indicator.width + check.spacing
            text: check.label
            verticalAlignment: Text.AlignVCenter
            color: Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: 11
        }
    }
}
