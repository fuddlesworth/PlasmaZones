// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import "WizardUtils.js" as WizardUtils
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

    required property var controller // SettingsController — CRUD + signals
    property int currentStep: 0
    property string selectedType: "custom"
    property int selectedAspectRatio: -1
    property bool openInEditor: true
    property string _previousAutoName: ""
    // Set for the duration of a create, so the button and the Return key
    // cannot both land a second createNewLayout call.
    property bool _creating: false
    // Match the monitor's aspect ratio for the preview, clamped to [1.0, 3.6]
    // so an extreme ratio (e.g. 32:9) keeps the preview usable.
    // Read through the content Item rather than this root. `Screen` on a Popup
    // resolves against no window of its own, so it answers for the primary
    // monitor: the preview took the primary display's ratio even when the
    // dialog was open on another one. Through the Item it follows the window,
    // and as a binding it keeps following it across screens rather than
    // sampling once on open.
    readonly property real screenAspectRatio: WizardUtils.clampedScreenAspectRatio(dialogContent.Screen.width, dialogContent.Screen.height)
    // Template previews match TemplateService strategies exactly
    // (see src/editor/services/TemplateService.cpp and core/constants.h)
    readonly property var templates: [
        {
            "name": i18n("Blank Canvas"),
            "type": "custom",
            "desc": i18n("Start from scratch in the editor"),
            "zones": [
                {
                    "relativeGeometry": {
                        "x": 0,
                        "y": 0,
                        "width": 1,
                        "height": 1
                    },
                    "zoneNumber": 1
                }
            ]
        },
        {
            "name": i18n("Columns"),
            "type": "columns",
            "desc": i18n("Two equal vertical columns"),
            "zones": [
                {
                    "relativeGeometry": {
                        "x": 0,
                        "y": 0,
                        "width": 0.5,
                        "height": 1
                    },
                    "zoneNumber": 1
                },
                {
                    "relativeGeometry": {
                        "x": 0.5,
                        "y": 0,
                        "width": 0.5,
                        "height": 1
                    },
                    "zoneNumber": 2
                }
            ]
        },
        {
            "name": i18n("Rows"),
            "type": "rows",
            "desc": i18n("Two equal horizontal rows"),
            "zones": [
                {
                    "relativeGeometry": {
                        "x": 0,
                        "y": 0,
                        "width": 1,
                        "height": 0.5
                    },
                    "zoneNumber": 1
                },
                {
                    "relativeGeometry": {
                        "x": 0,
                        "y": 0.5,
                        "width": 1,
                        "height": 0.5
                    },
                    "zoneNumber": 2
                }
            ]
        },
        {
            "name": i18n("Grid"),
            "type": "grid",
            "desc": i18n("2\u00d72 equal quadrants"),
            "zones": [
                {
                    "relativeGeometry": {
                        "x": 0,
                        "y": 0,
                        "width": 0.5,
                        "height": 0.5
                    },
                    "zoneNumber": 1
                },
                {
                    "relativeGeometry": {
                        "x": 0.5,
                        "y": 0,
                        "width": 0.5,
                        "height": 0.5
                    },
                    "zoneNumber": 2
                },
                {
                    "relativeGeometry": {
                        "x": 0,
                        "y": 0.5,
                        "width": 0.5,
                        "height": 0.5
                    },
                    "zoneNumber": 3
                },
                {
                    "relativeGeometry": {
                        "x": 0.5,
                        "y": 0.5,
                        "width": 0.5,
                        "height": 0.5
                    },
                    "zoneNumber": 4
                }
            ]
        },
        {
            "name": i18n("Priority"),
            "type": "priority",
            "desc": i18n("Large main + secondary stack"),
            "zones": [
                {
                    "relativeGeometry": {
                        "x": 0,
                        "y": 0,
                        "width": 0.667,
                        "height": 1
                    },
                    "zoneNumber": 1
                },
                {
                    "relativeGeometry": {
                        "x": 0.667,
                        "y": 0,
                        "width": 0.333,
                        "height": 0.5
                    },
                    "zoneNumber": 2
                },
                {
                    "relativeGeometry": {
                        "x": 0.667,
                        "y": 0.5,
                        "width": 0.333,
                        "height": 0.5
                    },
                    "zoneNumber": 3
                }
            ]
        },
        {
            "name": i18n("Focus"),
            "type": "focus",
            "desc": i18n("Center panel + side panels"),
            "zones": [
                {
                    "relativeGeometry": {
                        "x": 0,
                        "y": 0,
                        "width": 0.2,
                        "height": 1
                    },
                    "zoneNumber": 1
                },
                {
                    "relativeGeometry": {
                        "x": 0.2,
                        "y": 0,
                        "width": 0.6,
                        "height": 1
                    },
                    "zoneNumber": 2
                },
                {
                    "relativeGeometry": {
                        "x": 0.8,
                        "y": 0,
                        "width": 0.2,
                        "height": 1
                    },
                    "zoneNumber": 3
                }
            ]
        }
    ]
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
        root._creating = false;
        wizardFooter.errorText = "";
    }

    ColumnLayout {
        id: dialogContent

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
                WizardPreviewFrame {
                    aspectRatio: root.screenAspectRatio

                    QFZCommon.ZonePreview {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        // Space for the bottom name bar, same reservation as
                        // LayoutThumbnail makes for its label.
                        anchors.bottomMargin: Kirigami.Units.gridUnit * 1.5 + Kirigami.Units.smallSpacing
                        zones: root.selectedTemplate.zones
                        showZoneNumbers: true
                        isHovered: true
                        highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
                    }

                    WizardPreviewBadge {
                        text: root.selectedTemplate.name
                    }
                }

                // Config card
                WizardConfigCard {
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
                            // Mirrors PlasmaZones::MaxLayoutNameLength (core/constants.h),
                            // same client-side cap as the editor's layout name field.
                            maximumLength: 40
                            Accessible.name: i18n("Layout name")
                            Keys.onReturnPressed: {
                                if (wizardFooter.createEnabled)
                                    wizardFooter.createClicked();
                            }
                            Keys.onEnterPressed: {
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

                        // Index maps straight onto AspectRatioClass
                        // (Any=0, Standard=1, Ultrawide=2, SuperUltrawide=3,
                        // Portrait=4), the same way the editor's
                        // LayoutSettingsDialog and Main.qml's filter do. The
                        // value travels unmapped into setLayoutAspectRatioClass,
                        // so a shift here would tag every layout with the
                        // neighbouring class and put Portrait out of reach.
                        //
                        // selectedAspectRatio starts at -1 for "not chosen",
                        // which createNewLayout reads as "skip the call" and
                        // leaves the daemon's own Any default. From that
                        // initial state, clicking Any is swallowed rather than
                        // writing 0: the group already displays index 0 (the
                        // Math.max below clamps -1 to 0), so its same-index
                        // click guard eats the click and selectedAspectRatio
                        // stays -1. Once another class has been picked the
                        // index no longer matches, so clicking Any does write
                        // 0. The daemon default is Any, so the net behavior is
                        // the same either way.
                        SettingsButtonGroup {
                            model: [i18n("Any"), "16:9", "21:9", "32:9", i18n("Portrait")]
                            currentIndex: Math.max(0, root.selectedAspectRatio)
                            onIndexChanged: index => {
                                root.selectedAspectRatio = index;
                            }
                        }

                        Label {
                            visible: root.selectedAspectRatio <= 0
                            text: i18n("This layout is offered on every monitor")
                            font: Kirigami.Theme.smallFont
                            opacity: 0.4
                        }
                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    // Options
                    CheckBox {
                        // AbstractButton already names itself from `text`.
                        text: i18n("Open in editor after creation")
                        checked: root.openInEditor
                        onToggled: root.openInEditor = checked
                    }
                }
            }
        }
    }

    Connections {
        function onLayoutOperationFailed(reason) {
            if (root.opened)
                wizardFooter.errorText = reason;
        }

        target: root.controller
    }

    footer: WizardFooter {
        id: wizardFooter

        currentStep: root.currentStep
        createText: i18n("Create Layout")
        createEnabled: nameField.text.trim().length > 0 && !root._creating
        onBackClicked: root.currentStep = 0
        onNextClicked: root.currentStep = 1
        onCreateClicked: {
            // close() runs an exit transition during which the footer stays
            // live, so without this a second click would create a second
            // layout. Return key auto-repeat reaches the same handler.
            if (root._creating)
                return;

            root._creating = true;
            wizardFooter.errorText = "";
            if (root.controller.createNewLayout(nameField.text.trim(), root.selectedType, root.selectedAspectRatio, root.openInEditor)) {
                root.close();
            } else {
                // Creation failed and the dialog stays open showing why, so
                // release the guard for the retry.
                root._creating = false;
            }
        }
        onCancelClicked: root.close()
    }
}
