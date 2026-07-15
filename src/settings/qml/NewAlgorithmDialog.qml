// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import "WizardUtils.js" as WizardUtils
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Multi-step wizard for creating a new tiling algorithm.
 *
 * Step 1: Choose a base template (live algorithm previews)
 * Step 2: Configure name, capabilities, and review live preview
 */
Kirigami.Dialog {
    id: root

    required property var controller // SettingsController — provides generateAlgorithmPreview + CRUD methods
    property int currentStep: 0
    property string baseTemplate: "blank"
    property bool supportsMasterCount: false
    property bool supportsSplitRatio: false
    property bool producesOverlappingZones: false
    property bool supportsMemory: false
    property bool supportsScriptState: false
    property bool supportsSingleWindow: false
    property bool retileOnFocus: false
    property bool openInEditor: true
    property string _previousAutoName: ""
    // Set for the duration of a create, so the button and the Return key
    // cannot both land a second createNewAlgorithm call.
    property bool _creating: false
    readonly property var _colors: WizardUtils.wizardColors(Kirigami.Theme.textColor, Kirigami.Theme.highlightColor)
    readonly property color _subtleBg: _colors.subtleBg
    readonly property color _accentBorder: _colors.accentBorder
    // Clamped to [1.0, 3.6] to keep the preview usable on extreme aspect ratios (e.g. 32:9).
    // Read through the content Item rather than this root. `Screen` on a Popup
    // resolves against no window of its own, so it answers for the primary
    // monitor: the preview took the primary display's ratio even when the
    // dialog was open on another one. Through the Item it follows the window,
    // and as a binding it keeps following it across screens rather than
    // sampling once on open.
    readonly property real screenAspectRatio: WizardUtils.clampedScreenAspectRatio(dialogContent.Screen.width, dialogContent.Screen.height)
    readonly property var baseTemplates: [
        {
            "name": i18n("Blank"),
            "id": "blank",
            "desc": i18n("Minimal skeleton to implement yourself")
        },
        {
            "name": i18n("Master + Stack"),
            "id": "master-stack",
            "desc": i18n("Large master area with stacked windows")
        },
        {
            "name": i18n("Grid"),
            "id": "grid",
            "desc": i18n("Equal-sized NxM grid layout")
        },
        {
            "name": i18n("Binary Split"),
            "id": "bsp",
            "desc": i18n("Balanced recursive BSP splitting")
        },
        {
            "name": i18n("Aligned Grid"),
            "id": "aligned-grid",
            "desc": i18n("Resize-aware grid that moves whole rows and columns")
        },
        {
            "name": i18n("Dwindle (Memory)"),
            "id": "dwindle-memory",
            "desc": i18n("Remembers split positions when you resize a split")
        },
        {
            "name": i18n("Cluster"),
            "id": "cluster",
            "desc": i18n("Groups windows by application, with custom parameters")
        },
        {
            "name": i18n("Theater"),
            "id": "theater",
            "desc": i18n("Spotlight that follows focus, with windows on side rails")
        },
        {
            "name": i18n("Deck"),
            "id": "deck",
            "desc": i18n("Focused window on the left with the rest peeking from the right edge")
        }
    ]
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
        if (nameField.text === "" || nameField.text === root._previousAutoName) {
            let autoName = i18n("My %1", templateData.name);
            nameField.text = autoName;
            root._previousAutoName = autoName;
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
        root.supportsScriptState = false;
        root.supportsSingleWindow = false;
        root.retileOnFocus = false;
        root.openInEditor = true;
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

                // 3-column grid — Blank plus a curated subset of the bundled algorithms
                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: Kirigami.Units.mediumSpacing
                    rowSpacing: Kirigami.Units.mediumSpacing

                    Repeater {
                        model: root.baseTemplates

                        delegate: WizardTemplateCard {
                            id: templateDelegate

                            required property var modelData

                            templateName: modelData.name
                            templateDesc: modelData.desc
                            selected: root.baseTemplate === modelData.id
                            onClicked: root.selectTemplate(modelData)
                            onDoubleClicked: {
                                root.selectTemplate(modelData);
                                root.currentStep = 1;
                            }

                            AlgorithmPreview {
                                anchors.fill: parent
                                visible: templateDelegate.modelData.id !== "blank"
                                // The empty id is AlgorithmPreview's "nothing to
                                // render" contract: no lookup, no empty-result
                                // retry. Two things want it here. There is no
                                // blank.luau to preview. And this dialog is
                                // built with the Layouts page rather than on
                                // demand, so without the `opened` gate all eight
                                // template previews would run their Luau tile
                                // pass on every page load, for a wizard the user
                                // may never open.
                                algorithmId: (root.opened && templateDelegate.modelData.id !== "blank") ? templateDelegate.modelData.id : ""
                                windowCount: 4
                                showLabel: false
                                appSettings: root.controller
                            }

                            Kirigami.Icon {
                                anchors.centerIn: parent
                                visible: templateDelegate.modelData.id === "blank"
                                source: "code-context"
                                implicitWidth: Kirigami.Units.iconSizes.huge
                                implicitHeight: Kirigami.Units.iconSizes.huge
                                color: templateDelegate.selected ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor

                                Behavior on color {
                                    PhosphorMotionAnimation {
                                        profile: "widget.tint"
                                        durationOverride: Kirigami.Units.longDuration
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
                    // Let the column size this and cap it, rather than reading
                    // parent.width upward: a layout child asking its parent how
                    // wide it is asks the question the parent is still
                    // answering. The height follows the width the layout
                    // actually assigned.
                    //
                    // The 12-grid-unit height ceiling is folded into the width
                    // cap rather than set as its own Layout.maximumHeight. A
                    // standalone height cap overrides preferredHeight and
                    // reshapes the box to whatever ratio the two caps form,
                    // which discarded the monitor ratio on every display
                    // narrower than 26:12. Capping width by the ratio holds the
                    // same ceiling while keeping the box on-ratio.
                    Layout.fillWidth: true
                    Layout.maximumWidth: Math.min(Kirigami.Units.gridUnit * 26, Kirigami.Units.gridUnit * 12 * root.screenAspectRatio)
                    Layout.preferredHeight: width / root.screenAspectRatio
                    Layout.alignment: Qt.AlignHCenter
                    radius: Kirigami.Units.smallSpacing * 2
                    color: root._subtleBg
                    border.width: Math.round(Screen.devicePixelRatio)
                    border.color: root._accentBorder

                    AlgorithmPreview {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        anchors.bottomMargin: Kirigami.Units.largeSpacing * 2
                        visible: root.baseTemplate !== "blank"
                        algorithmId: root.baseTemplate === "blank" ? "" : root.baseTemplate
                        appSettings: root.controller
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
                            text: i18n("Algorithm Name")
                            font.weight: Font.DemiBold
                            opacity: 0.7
                        }

                        TextField {
                            id: nameField

                            Layout.fillWidth: true
                            placeholderText: i18n("My Algorithm")
                            Accessible.name: i18n("Algorithm name")
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

                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    // Capabilities — editable for the blank scaffold only. A
                    // template's capabilities are part of its metadata and
                    // travel with the copied script.
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
                                text: i18n("(editable later in the .luau file)")
                                font: Kirigami.Theme.smallFont
                                opacity: 0.4
                            }
                        }

                        Label {
                            visible: root.baseTemplate !== "blank"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: i18n("Capabilities are inherited from the template so its layout code keeps working.")
                            font: Kirigami.Theme.smallFont
                            opacity: 0.6
                        }

                        GridLayout {
                            visible: root.baseTemplate === "blank"
                            columns: 2
                            columnSpacing: Kirigami.Units.largeSpacing * 2
                            rowSpacing: Kirigami.Units.smallSpacing

                            // Each box drives its flag through `onToggled` and
                            // reads it back through a binding, so the onOpened
                            // resets land on the control. There is no
                            // write-back loop: toggle() reaches `checked`
                            // through a C++ setter, which neither severs the
                            // binding nor re-emits toggled(). `Binding on
                            // checked` is equivalent to a plain `checked:`
                            // binding here, and its RestoreNone never comes up
                            // because these bindings carry no `when` and so
                            // never deactivate.
                            CheckBox {
                                text: i18n("Master count")
                                onToggled: root.supportsMasterCount = checked
                                Accessible.description: ToolTip.text
                                ToolTip.visible: hovered
                                ToolTip.delay: Kirigami.Units.toolTipDelay
                                ToolTip.text: i18n("Configurable master/center windows")

                                Binding on checked {
                                    value: root.supportsMasterCount
                                    restoreMode: Binding.RestoreNone
                                }
                            }

                            CheckBox {
                                text: i18n("Split ratio")
                                onToggled: root.supportsSplitRatio = checked
                                Accessible.description: ToolTip.text
                                ToolTip.visible: hovered
                                ToolTip.delay: Kirigami.Units.toolTipDelay
                                ToolTip.text: i18n("Adjustable master/stack ratio")

                                Binding on checked {
                                    value: root.supportsSplitRatio
                                    restoreMode: Binding.RestoreNone
                                }
                            }

                            CheckBox {
                                text: i18n("Overlapping zones")
                                onToggled: root.producesOverlappingZones = checked
                                Accessible.description: ToolTip.text
                                ToolTip.visible: hovered
                                ToolTip.delay: Kirigami.Units.toolTipDelay
                                ToolTip.text: i18n("Zones can overlap each other")

                                Binding on checked {
                                    value: root.producesOverlappingZones
                                    restoreMode: Binding.RestoreNone
                                }
                            }

                            CheckBox {
                                text: i18n("Persistent memory")
                                onToggled: root.supportsMemory = checked
                                Accessible.description: ToolTip.text
                                ToolTip.visible: hovered
                                ToolTip.delay: Kirigami.Units.toolTipDelay
                                ToolTip.text: i18n("Remembers positions across changes")

                                Binding on checked {
                                    value: root.supportsMemory
                                    restoreMode: Binding.RestoreNone
                                }
                            }

                            CheckBox {
                                text: i18n("Script state")
                                onToggled: root.supportsScriptState = checked
                                Accessible.description: ToolTip.text
                                ToolTip.visible: hovered
                                ToolTip.delay: Kirigami.Units.toolTipDelay
                                ToolTip.text: i18n("Keeps a persistent state table across retiles")

                                Binding on checked {
                                    value: root.supportsScriptState
                                    restoreMode: Binding.RestoreNone
                                }
                            }

                            CheckBox {
                                text: i18n("Single window")
                                onToggled: root.supportsSingleWindow = checked
                                Accessible.description: ToolTip.text
                                ToolTip.visible: hovered
                                ToolTip.delay: Kirigami.Units.toolTipDelay
                                ToolTip.text: i18n("Lays out a lone window itself instead of filling the screen")

                                Binding on checked {
                                    value: root.supportsSingleWindow
                                    restoreMode: Binding.RestoreNone
                                }
                            }

                            CheckBox {
                                text: i18n("Follows focus")
                                onToggled: root.retileOnFocus = checked
                                Accessible.description: ToolTip.text
                                ToolTip.visible: hovered
                                ToolTip.delay: Kirigami.Units.toolTipDelay
                                ToolTip.text: i18n("Reflows when focus moves between tiled windows")

                                Binding on checked {
                                    value: root.retileOnFocus
                                    restoreMode: Binding.RestoreNone
                                }
                            }
                        }
                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    // Options
                    CheckBox {
                        // No Accessible.name: CheckBox already exposes `text`.
                        // No tooltip here either, so there is nothing to add as
                        // a description.
                        text: i18n("Open in text editor after creation")
                        onToggled: root.openInEditor = checked

                        Binding on checked {
                            value: root.openInEditor
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }
            }
        }
    }

    Connections {
        function onAlgorithmOperationFailed(reason) {
            // Only show inline error when the dialog is visible — otherwise
            // LayoutsPage's toast handler will surface the error (avoids double reporting)
            if (root.opened)
                wizardFooter.errorText = reason;
        }

        target: root.controller
    }

    footer: WizardFooter {
        id: wizardFooter

        currentStep: root.currentStep
        createText: i18n("Create Algorithm")
        createEnabled: nameField.text.trim().length > 0 && !root._creating
        onBackClicked: root.currentStep = 0
        onNextClicked: root.currentStep = 1
        onCreateClicked: {
            // close() runs an exit transition during which the footer stays
            // live, so without this a second click would create a second
            // algorithm under the rolled-over "<name>-1" filename.
            if (root._creating)
                return;

            root._creating = true;
            wizardFooter.errorText = "";
            let result = root.controller.createNewAlgorithm(nameField.text.trim(), root.baseTemplate, {
                "supportsMasterCount": root.supportsMasterCount,
                "supportsSplitRatio": root.supportsSplitRatio,
                "producesOverlappingZones": root.producesOverlappingZones,
                "supportsMemory": root.supportsMemory,
                "supportsScriptState": root.supportsScriptState,
                "supportsSingleWindow": root.supportsSingleWindow,
                "retileOnFocus": root.retileOnFocus
            });
            if (result && result.length > 0) {
                if (root.openInEditor)
                    root.controller.openAlgorithm(result);

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
