// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Multi-step wizard for creating a new tiling algorithm.
 *
 * Step 1: Choose a base template (live algorithm previews)
 * Step 2: Configure name, capabilities, and review live preview
 */
Kirigami.Dialog {
    id: root

    required property var appSettings
    property int currentStep: 0
    property string baseTemplate: "blank"
    property bool supportsMasterCount: false
    property bool supportsSplitRatio: false
    property bool producesOverlappingZones: false
    property bool supportsMemory: false
    property string _previousAutoName: ""
    readonly property var baseTemplates: [{
        "name": i18n("Blank"),
        "id": "blank",
        "desc": i18n("Minimal skeleton to implement yourself"),
        "hasMaster": false,
        "hasSplit": false
    }, {
        "name": i18n("Master + Stack"),
        "id": "master-stack",
        "desc": i18n("Large master area with stacked windows"),
        "hasMaster": true,
        "hasSplit": true
    }, {
        "name": i18n("Grid"),
        "id": "grid",
        "desc": i18n("Equal-sized NxM grid layout"),
        "hasMaster": false,
        "hasSplit": false
    }, {
        "name": i18n("Binary Split"),
        "id": "bsp",
        "desc": i18n("Balanced recursive BSP splitting"),
        "hasMaster": false,
        "hasSplit": true
    }]
    // Resolve selected template data for step 2
    readonly property var selectedTemplate: {
        for (let i = 0; i < baseTemplates.length; i++) {
            if (baseTemplates[i].id === root.baseTemplate)
                return baseTemplates[i];

        }
        return baseTemplates[0];
    }

    function selectTemplate(templateData) {
        root.baseTemplate = templateData.id;
        root.supportsMasterCount = templateData.hasMaster;
        root.supportsSplitRatio = templateData.hasSplit;
        root.producesOverlappingZones = false;
        root.supportsMemory = false;
        if (nameField.text === "" || nameField.text === root._previousAutoName) {
            let auto_name = i18n("My %1", templateData.name);
            nameField.text = auto_name;
            root._previousAutoName = auto_name;
        }
    }

    title: i18nc("@title:window", "New Algorithm")
    preferredWidth: Math.min(Kirigami.Units.gridUnit * 40, parent ? parent.width * 0.9 : Kirigami.Units.gridUnit * 40)
    standardButtons: Kirigami.Dialog.NoButton
    padding: Kirigami.Units.largeSpacing
    onOpened: {
        root.currentStep = 0;
        root.baseTemplate = "blank";
        nameField.text = "";
        root._previousAutoName = "";
        root.supportsMasterCount = false;
        root.supportsSplitRatio = false;
        root.producesOverlappingZones = false;
        root.supportsMemory = false;
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // ── Step indicator ─────────────────────────────────────────────
        WizardStepIndicator {
            stepLabels: [i18n("Template"), i18n("Configure")]
            currentStep: root.currentStep
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // ── Page stack ─────────────────────────────────────────────────
        StackLayout {
            Layout.fillWidth: true
            currentIndex: root.currentStep

            // ── Step 1: Template picker ────────────────────────────────
            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                Label {
                    text: i18n("Choose a starting point")
                    font.weight: Font.DemiBold
                    Layout.alignment: Qt.AlignHCenter
                    opacity: 0.7
                }

                // 2x2 grid — matches layout wizard's visual pattern
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Kirigami.Units.mediumSpacing
                    rowSpacing: Kirigami.Units.mediumSpacing

                    Repeater {
                        model: root.baseTemplates

                        delegate: Item {
                            id: templateDelegate

                            required property var modelData
                            required property int index
                            readonly property bool isSelected: root.baseTemplate === modelData.id
                            property bool isHovered: false

                            Layout.fillWidth: true
                            Layout.preferredHeight: Kirigami.Units.gridUnit * 10
                            Accessible.name: modelData.name
                            Accessible.description: modelData.desc
                            Accessible.role: Accessible.Button

                            HoverHandler {
                                onHoveredChanged: templateDelegate.isHovered = hovered
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: false
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.selectTemplate(modelData)
                                onDoubleClicked: {
                                    root.selectTemplate(modelData);
                                    root.currentStep = 1;
                                }
                            }

                            Rectangle {
                                id: templateCard

                                anchors.fill: parent
                                radius: Kirigami.Units.smallSpacing * 2
                                color: {
                                    if (templateDelegate.isSelected)
                                        return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15);

                                    if (templateDelegate.isHovered)
                                        return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06);

                                    return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03);
                                }
                                border.width: templateDelegate.isSelected ? Math.round(Kirigami.Units.devicePixelRatio * 2) : Math.round(Kirigami.Units.devicePixelRatio)
                                border.color: {
                                    if (templateDelegate.isSelected)
                                        return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.6);

                                    if (templateDelegate.isHovered)
                                        return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.3);

                                    return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08);
                                }
                                transform: [
                                    Scale {
                                        origin.x: templateCard.width / 2
                                        origin.y: templateCard.height / 2
                                        xScale: templateDelegate.isHovered ? 1.02 : 1
                                        yScale: templateDelegate.isHovered ? 1.02 : 1

                                        Behavior on xScale {
                                            NumberAnimation {
                                                duration: 200
                                                easing.type: Easing.OutCubic
                                            }

                                        }

                                        Behavior on yScale {
                                            NumberAnimation {
                                                duration: 200
                                                easing.type: Easing.OutCubic
                                            }

                                        }

                                    },
                                    Translate {
                                        y: templateDelegate.isHovered ? -1 : 0

                                        Behavior on y {
                                            NumberAnimation {
                                                duration: 200
                                                easing.type: Easing.OutCubic
                                            }

                                        }

                                    }
                                ]

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: Kirigami.Units.smallSpacing * 2
                                    spacing: Kirigami.Units.smallSpacing

                                    Item {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true

                                        AlgorithmPreview {
                                            anchors.fill: parent
                                            visible: templateDelegate.modelData.id !== "blank"
                                            algorithmId: templateDelegate.modelData.id
                                            windowCount: 4
                                            showLabel: false
                                            appSettings: root.appSettings
                                        }

                                        Kirigami.Icon {
                                            anchors.centerIn: parent
                                            visible: templateDelegate.modelData.id === "blank"
                                            source: "code-context"
                                            implicitWidth: Kirigami.Units.iconSizes.huge
                                            implicitHeight: Kirigami.Units.iconSizes.huge
                                            color: templateDelegate.isSelected ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor

                                            Behavior on color {
                                                ColorAnimation {
                                                    duration: 200
                                                }

                                            }

                                        }

                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: templateDelegate.modelData.name
                                        font.weight: templateDelegate.isSelected ? Font.Bold : Font.Normal
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: templateDelegate.modelData.desc
                                        font: Kirigami.Theme.smallFont
                                        horizontalAlignment: Text.AlignHCenter
                                        opacity: 0.5
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 2
                                    }

                                }

                                // Selected badge
                                Rectangle {
                                    anchors.top: parent.top
                                    anchors.right: parent.right
                                    anchors.margins: Kirigami.Units.smallSpacing
                                    width: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                                    height: width
                                    radius: width / 2
                                    color: Kirigami.Theme.highlightColor
                                    visible: templateDelegate.isSelected

                                    Kirigami.Icon {
                                        anchors.centerIn: parent
                                        source: "checkmark"
                                        width: Kirigami.Units.iconSizes.small
                                        height: Kirigami.Units.iconSizes.small
                                        color: Kirigami.Theme.highlightedTextColor
                                    }

                                }

                                Behavior on color {
                                    ColorAnimation {
                                        duration: 200
                                        easing.type: Easing.OutCubic
                                    }

                                }

                                Behavior on border.color {
                                    ColorAnimation {
                                        duration: 200
                                        easing.type: Easing.OutCubic
                                    }

                                }

                                Behavior on border.width {
                                    NumberAnimation {
                                        duration: 150
                                    }

                                }

                            }

                        }

                    }

                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("Double-click a template to skip ahead")
                    font: Kirigami.Theme.smallFont
                    opacity: 0.4
                }

            }

            // ── Step 2: Configure ──────────────────────────────────────
            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                // Landscape preview matching monitor aspect ratio
                Rectangle {
                    readonly property real screenAspectRatio: Screen.width > 0 && Screen.height > 0 ? (Screen.width / Screen.height) : (16 / 9)

                    Layout.preferredWidth: Math.min(Kirigami.Units.gridUnit * 26, parent ? parent.width : Kirigami.Units.gridUnit * 26)
                    Layout.preferredHeight: Layout.preferredWidth / screenAspectRatio
                    Layout.maximumHeight: Kirigami.Units.gridUnit * 12
                    Layout.alignment: Qt.AlignHCenter
                    radius: Kirigami.Units.smallSpacing * 2
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03)
                    border.width: Math.round(Kirigami.Units.devicePixelRatio)
                    border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.3)

                    AlgorithmPreview {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        anchors.bottomMargin: Kirigami.Units.largeSpacing * 2
                        visible: root.baseTemplate !== "blank"
                        algorithmId: root.baseTemplate
                        appSettings: root.appSettings
                        windowCount: 6
                        showLabel: false
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        visible: root.baseTemplate === "blank"
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: "code-context"
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: Kirigami.Units.iconSizes.huge
                            implicitHeight: Kirigami.Units.iconSizes.huge
                            color: Kirigami.Theme.disabledTextColor
                        }

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: i18n("Blank skeleton")
                            font: Kirigami.Theme.smallFont
                            opacity: 0.5
                        }

                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottomMargin: Kirigami.Units.smallSpacing
                        width: algoBadgeLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
                        height: algoBadgeLabel.implicitHeight + Kirigami.Units.smallSpacing * 2
                        radius: height / 2
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2)
                        border.width: Math.round(Kirigami.Units.devicePixelRatio)
                        border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4)

                        Label {
                            id: algoBadgeLabel

                            anchors.centerIn: parent
                            text: root.selectedTemplate.name
                            font.weight: Font.DemiBold
                        }

                    }

                }

                // Config card
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: algoConfigColumn.implicitHeight + Kirigami.Units.largeSpacing * 2
                    radius: Kirigami.Units.smallSpacing * 2
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03)
                    border.width: Math.round(Kirigami.Units.devicePixelRatio)
                    border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)

                    ColumnLayout {
                        id: algoConfigColumn

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.largeSpacing

                        // Name
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Label {
                                text: i18n("Algorithm Name")
                                font.weight: Font.DemiBold
                                opacity: 0.7
                            }

                            TextField {
                                id: nameField

                                Layout.fillWidth: true
                                placeholderText: i18n("My Algorithm")
                                Accessible.name: i18n("Algorithm name")
                                Component.onCompleted: {
                                    root.currentStepChanged.connect(function() {
                                        if (root.currentStep === 1)
                                            nameField.forceActiveFocus();

                                    });
                                }
                                Keys.onReturnPressed: {
                                    if (wizardFooter.createEnabled)
                                        wizardFooter.createClicked();

                                }
                            }

                        }

                        Kirigami.Separator {
                            Layout.fillWidth: true
                        }

                        // Capabilities
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            RowLayout {
                                spacing: Kirigami.Units.mediumSpacing

                                Label {
                                    text: i18n("Capabilities")
                                    font.weight: Font.DemiBold
                                    opacity: 0.7
                                }

                                Label {
                                    text: i18n("(editable later in the .js file)")
                                    font: Kirigami.Theme.smallFont
                                    opacity: 0.4
                                }

                            }

                            GridLayout {
                                columns: 2
                                columnSpacing: Kirigami.Units.largeSpacing * 2
                                rowSpacing: Kirigami.Units.smallSpacing

                                CheckBox {
                                    text: i18n("Master count")
                                    checked: root.supportsMasterCount
                                    onToggled: root.supportsMasterCount = checked
                                    Accessible.name: i18n("Supports master count")
                                    ToolTip.visible: hovered
                                    ToolTip.delay: Kirigami.Units.toolTipDelay
                                    ToolTip.text: i18n("Configurable master/center windows")
                                }

                                CheckBox {
                                    text: i18n("Split ratio")
                                    checked: root.supportsSplitRatio
                                    onToggled: root.supportsSplitRatio = checked
                                    Accessible.name: i18n("Supports split ratio")
                                    ToolTip.visible: hovered
                                    ToolTip.delay: Kirigami.Units.toolTipDelay
                                    ToolTip.text: i18n("Adjustable master/stack ratio")
                                }

                                CheckBox {
                                    text: i18n("Overlapping zones")
                                    checked: root.producesOverlappingZones
                                    onToggled: root.producesOverlappingZones = checked
                                    Accessible.name: i18n("Produces overlapping zones")
                                    ToolTip.visible: hovered
                                    ToolTip.delay: Kirigami.Units.toolTipDelay
                                    ToolTip.text: i18n("Zones can overlap each other")
                                }

                                CheckBox {
                                    text: i18n("Persistent memory")
                                    checked: root.supportsMemory
                                    onToggled: root.supportsMemory = checked
                                    Accessible.name: i18n("Remembers split positions")
                                    ToolTip.visible: hovered
                                    ToolTip.delay: Kirigami.Units.toolTipDelay
                                    ToolTip.text: i18n("Remembers positions across changes")
                                }

                            }

                        }

                    }

                }

            }

        }

    }

    footer: WizardFooter {
        id: wizardFooter

        currentStep: root.currentStep
        createText: i18n("Create Algorithm")
        createEnabled: nameField.text.trim().length > 0
        onBackClicked: root.currentStep = 0
        onNextClicked: root.currentStep = 1
        onCreateClicked: {
            settingsController.createNewAlgorithm(nameField.text.trim(), root.baseTemplate, root.supportsMasterCount, root.supportsSplitRatio, root.producesOverlappingZones, root.supportsMemory);
            root.close();
        }
        onCancelClicked: root.close()
    }

}
