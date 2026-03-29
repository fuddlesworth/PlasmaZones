// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * @brief Multi-step wizard for creating a new snapping layout.
 *
 * Step 1: Choose a template (visual grid of zone previews)
 * Step 2: Configure name, aspect ratio, and options
 */
Kirigami.Dialog {
    id: root

    required property var appSettings // Settings object (settingsBridge)
    required property var controller // SettingsController — CRUD + signals
    property int currentStep: 0
    property string selectedType: "custom"
    property int selectedAspectRatio: -1
    property bool openInEditor: true
    property string _previousAutoName: ""
    readonly property color _subtleBg: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03)
    readonly property color _subtleBorder: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
    readonly property color _accentBorder: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.3)
    // Match the user's primary monitor aspect ratio for the preview.
    // Re-evaluated on open so it picks up the correct screen even if the
    // dialog is constructed before the window is shown on a display.
    // Clamped to [1.0, 3.6] to keep the preview usable on extreme aspect ratios (e.g. 32:9).
    property real screenAspectRatio: 16 / 9
    // Template previews match TemplateService strategies exactly
    // (see src/editor/services/TemplateService.cpp and core/constants.h)
    readonly property var templates: [{
        "name": i18n("Blank Canvas"),
        "type": "custom",
        "desc": i18n("Start from scratch in the editor"),
        "zones": [{
            "relativeGeometry": {
                "x": 0,
                "y": 0,
                "width": 1,
                "height": 1
            },
            "zoneNumber": 1
        }]
    }, {
        "name": i18n("Columns"),
        "type": "columns",
        "desc": i18n("Two equal vertical columns"),
        "zones": [{
            "relativeGeometry": {
                "x": 0,
                "y": 0,
                "width": 0.5,
                "height": 1
            },
            "zoneNumber": 1
        }, {
            "relativeGeometry": {
                "x": 0.5,
                "y": 0,
                "width": 0.5,
                "height": 1
            },
            "zoneNumber": 2
        }]
    }, {
        "name": i18n("Rows"),
        "type": "rows",
        "desc": i18n("Two equal horizontal rows"),
        "zones": [{
            "relativeGeometry": {
                "x": 0,
                "y": 0,
                "width": 1,
                "height": 0.5
            },
            "zoneNumber": 1
        }, {
            "relativeGeometry": {
                "x": 0,
                "y": 0.5,
                "width": 1,
                "height": 0.5
            },
            "zoneNumber": 2
        }]
    }, {
        "name": i18n("Grid"),
        "type": "grid",
        "desc": i18n("2\u00d72 equal quadrants"),
        "zones": [{
            "relativeGeometry": {
                "x": 0,
                "y": 0,
                "width": 0.5,
                "height": 0.5
            },
            "zoneNumber": 1
        }, {
            "relativeGeometry": {
                "x": 0.5,
                "y": 0,
                "width": 0.5,
                "height": 0.5
            },
            "zoneNumber": 2
        }, {
            "relativeGeometry": {
                "x": 0,
                "y": 0.5,
                "width": 0.5,
                "height": 0.5
            },
            "zoneNumber": 3
        }, {
            "relativeGeometry": {
                "x": 0.5,
                "y": 0.5,
                "width": 0.5,
                "height": 0.5
            },
            "zoneNumber": 4
        }]
    }, {
        "name": i18n("Priority"),
        "type": "priority",
        "desc": i18n("Large main + secondary stack"),
        "zones": [{
            "relativeGeometry": {
                "x": 0,
                "y": 0,
                "width": 0.667,
                "height": 1
            },
            "zoneNumber": 1
        }, {
            "relativeGeometry": {
                "x": 0.667,
                "y": 0,
                "width": 0.333,
                "height": 0.5
            },
            "zoneNumber": 2
        }, {
            "relativeGeometry": {
                "x": 0.667,
                "y": 0.5,
                "width": 0.333,
                "height": 0.5
            },
            "zoneNumber": 3
        }]
    }, {
        "name": i18n("Focus"),
        "type": "focus",
        "desc": i18n("Center panel + side panels"),
        "zones": [{
            "relativeGeometry": {
                "x": 0,
                "y": 0,
                "width": 0.2,
                "height": 1
            },
            "zoneNumber": 1
        }, {
            "relativeGeometry": {
                "x": 0.2,
                "y": 0,
                "width": 0.6,
                "height": 1
            },
            "zoneNumber": 2
        }, {
            "relativeGeometry": {
                "x": 0.8,
                "y": 0,
                "width": 0.2,
                "height": 1
            },
            "zoneNumber": 3
        }]
    }]
    // Resolve the selected template's data for step 2 preview
    readonly property var selectedTemplate: {
        for (let i = 0; i < templates.length; i++) {
            if (templates[i].type === root.selectedType)
                return templates[i];

        }
        return templates[0];
    }

    function selectTemplate(templateData) {
        root.selectedType = templateData.type;
        if (nameField.text === "" || nameField.text === root._previousAutoName) {
            let autoName = i18n("My %1 Layout", templateData.name);
            nameField.text = autoName;
            root._previousAutoName = autoName;
        }
    }

    title: i18nc("@title:window", "New Layout")
    preferredWidth: Math.min(Kirigami.Units.gridUnit * 40, parent ? parent.width * 0.9 : Kirigami.Units.gridUnit * 40)
    standardButtons: Kirigami.Dialog.NoButton
    padding: Kirigami.Units.largeSpacing
    onOpened: {
        root.currentStep = 0;
        root.selectedType = "custom";
        root.selectedAspectRatio = -1;
        root.openInEditor = true;
        nameField.text = "";
        root._previousAutoName = "";
        root.screenAspectRatio = (Screen.width > 0 && Screen.height > 0) ? Math.max(1, Math.min(3.6, Screen.width / Screen.height)) : (16 / 9);
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
            id: pageStack

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

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: Kirigami.Units.mediumSpacing
                    rowSpacing: Kirigami.Units.mediumSpacing

                    Repeater {
                        model: root.templates

                        delegate: WizardTemplateCard {
                            id: templateDelegate

                            required property var modelData
                            required property int index

                            templateName: templateDelegate.modelData.name
                            templateDesc: templateDelegate.modelData.desc
                            selected: root.selectedType === templateDelegate.modelData.type
                            onClicked: root.selectTemplate(templateDelegate.modelData)
                            onDoubleClicked: {
                                root.selectTemplate(templateDelegate.modelData);
                                root.currentStep = 1;
                            }

                            QFZCommon.ZonePreview {
                                anchors.fill: parent
                                zones: templateDelegate.modelData.zones
                                showZoneNumbers: true
                                isHovered: templateDelegate.isHovered || templateDelegate.selected
                                highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, templateDelegate.selected ? 0.8 : 0.5)
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
                    Layout.preferredWidth: Math.min(Kirigami.Units.gridUnit * 26, parent ? parent.width : Kirigami.Units.gridUnit * 26)
                    Layout.preferredHeight: Layout.preferredWidth / root.screenAspectRatio
                    Layout.maximumHeight: Kirigami.Units.gridUnit * 12
                    Layout.alignment: Qt.AlignHCenter
                    radius: Kirigami.Units.smallSpacing * 2
                    color: root._subtleBg
                    border.width: Math.round(Kirigami.Units.devicePixelRatio)
                    border.color: root._accentBorder

                    QFZCommon.ZonePreview {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        anchors.bottomMargin: Kirigami.Units.largeSpacing * 2
                        zones: root.selectedTemplate.zones
                        showZoneNumbers: true
                        isHovered: true
                        highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottomMargin: Kirigami.Units.smallSpacing
                        width: templateBadgeLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
                        height: templateBadgeLabel.implicitHeight + Kirigami.Units.smallSpacing * 2
                        radius: height / 2
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2)
                        border.width: Math.round(Kirigami.Units.devicePixelRatio)
                        border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4)

                        Label {
                            id: templateBadgeLabel

                            anchors.centerIn: parent
                            text: root.selectedTemplate.name
                            font.weight: Font.DemiBold
                        }

                    }

                }

                // Config card
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: configColumn.implicitHeight + Kirigami.Units.largeSpacing * 2
                    radius: Kirigami.Units.smallSpacing * 2
                    color: root._subtleBg
                    border.width: Math.round(Kirigami.Units.devicePixelRatio)
                    border.color: root._subtleBorder

                    ColumnLayout {
                        id: configColumn

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
                                text: i18n("Layout Name")
                                font.weight: Font.DemiBold
                                opacity: 0.7
                            }

                            TextField {
                                id: nameField

                                Layout.fillWidth: true
                                placeholderText: i18n("My Layout")
                                Accessible.name: i18n("Layout name")
                                Keys.onReturnPressed: {
                                    if (wizardFooter.createEnabled)
                                        wizardFooter.createClicked();

                                }
                            }

                            Connections {
                                function onCurrentStepChanged() {
                                    if (root.currentStep === 1)
                                        nameField.forceActiveFocus();

                                }

                                target: root
                            }

                        }

                        // Aspect ratio
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Label {
                                text: i18n("Aspect Ratio")
                                font.weight: Font.DemiBold
                                opacity: 0.7
                            }

                            SettingsButtonGroup {
                                model: [i18n("Auto"), "16:9", "21:9", "32:9", i18n("Portrait")]
                                currentIndex: root.selectedAspectRatio + 1
                                onIndexChanged: (index) => {
                                    root.selectedAspectRatio = index - 1;
                                }
                            }

                            Label {
                                visible: root.selectedAspectRatio === -1
                                text: i18n("Auto-detected from your primary monitor")
                                font: Kirigami.Theme.smallFont
                                opacity: 0.4
                            }

                        }

                        Kirigami.Separator {
                            Layout.fillWidth: true
                        }

                        // Options
                        CheckBox {
                            text: i18n("Open in editor after creation")
                            checked: root.openInEditor
                            onToggled: root.openInEditor = checked
                            Accessible.name: text
                        }

                    }

                }

            }

        }

    }

    Connections {
        function onLayoutCreationFailed(reason) {
            wizardFooter.errorText = reason;
        }

        target: root.controller
    }

    footer: WizardFooter {
        id: wizardFooter

        currentStep: root.currentStep
        createText: i18n("Create Layout")
        createEnabled: nameField.text.trim().length > 0
        onBackClicked: root.currentStep = 0
        onNextClicked: root.currentStep = 1
        onCreateClicked: {
            wizardFooter.errorText = "";
            if (root.controller.createNewLayout(nameField.text.trim(), root.selectedType, root.selectedAspectRatio, root.openInEditor))
                root.close();

        }
        onCancelClicked: root.close()
    }

}
