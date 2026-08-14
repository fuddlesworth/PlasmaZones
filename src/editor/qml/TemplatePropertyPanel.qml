// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ThemeHelpers.js" as Theme
import org.kde.kirigami as Kirigami

/**
 * @brief Right-side panel for scrolling-template editing
 *
 * The form half of template mode: description, the later-columns default
 * width, and the preset vocabularies. The spatial half (the blueprint
 * columns) lives on TemplateStripCanvas. Preset stops are edited as chips
 * (PresetChipEditor) rather than typed fraction lists, and the preset-kind
 * default picks from a combo of the actual stops rather than a bare index.
 * Field sync is imperative (syncFromController + Connections) rather than
 * bound, so a control the user is interacting with is never yanked by its
 * own write-back and the combo indexOfValue-in-binding trap stays out of
 * this file.
 */
Rectangle {
    id: templatePanel

    // Panel body resolves against the View color set, matching PropertyPanel
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    required property var editorController
    // The scrolling-template sub-model (editorController.scrollingTemplate)
    readonly property var templateModel: editorController ? editorController.scrollingTemplate : null
    required property bool chromeVisible

    readonly property var constants: templateModel ? templateModel.scrollingConstants() : ({})
    readonly property var displayOptions: [i18n("Stacked"), i18n("Tabbed")]
    readonly property int proportionMinPercent: Math.round((constants.proportionMin || 0.05) * 100)
    readonly property int proportionMaxPercent: Math.round((constants.proportionMax || 1) * 100)
    // The chips commit every edit straight to the model, so the live stop
    // count reads from the model rather than from a half-typed text field.
    readonly property int presetWidthCount: templateModel ? (templateModel.presetWidths || []).length : 0
    readonly property bool presetDefaultNeedsWidths: defaultWidthKindCombo.currentValue === constants.kindPreset && presetWidthCount === 0

    function syncDefaults() {
        if (!templateModel)
            return;

        const kindIndex = defaultWidthKindCombo.indexOfValue(templateModel.defaultWidthKind);
        defaultWidthKindCombo.currentIndex = kindIndex >= 0 ? kindIndex : defaultWidthKindCombo.indexOfValue(constants.kindPreset);
        // Fixed stores a pixel count, the fraction kinds a 0..1 fraction the
        // spin shows as percent.
        const isFixed = templateModel.defaultWidthKind === constants.kindFixed;
        defaultWidthValueSpin.value = isFixed ? Math.round(templateModel.defaultWidthValue) : Math.round(templateModel.defaultWidthValue * 100);
        // Clamp the stored index against the live stop list so a shrunk
        // vocabulary never leaves the combo pointing past its model.
        defaultPresetCombo.currentIndex = Math.min(templateModel.defaultPresetIndex, templatePanel.presetWidthCount - 1);
        defaultDisplayCombo.currentIndex = templateModel.defaultDisplay === 1 ? 1 : 0;
    }

    function syncFromController() {
        if (!templateModel)
            return;

        if (!descriptionField.activeFocus)
            descriptionField.text = templateModel.description || "";
        syncDefaults();
    }

    Layout.preferredWidth: chromeVisible ? Kirigami.Units.gridUnit * 20 : 0
    Layout.maximumWidth: Layout.preferredWidth
    Layout.fillHeight: true
    visible: chromeVisible
    color: Theme.withAlpha(Kirigami.Theme.backgroundColor, Theme.toolbarAlpha)

    Component.onCompleted: syncFromController()

    Connections {
        function onDescriptionChanged() {
            if (!descriptionField.activeFocus)
                descriptionField.text = templatePanel.templateModel.description || "";
        }

        function onDefaultsChanged() {
            templatePanel.syncDefaults();
        }

        // The stop list feeds the preset combo's model, so a preset edit
        // re-syncs the defaults too (the combo rebuild resets its index).
        function onPresetsChanged() {
            templatePanel.syncDefaults();
        }

        target: templatePanel.templateModel
        enabled: templatePanel.templateModel !== null
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: Kirigami.Units.largeSpacing

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.largeSpacing
                visible: templatePanel.templateModel ? templatePanel.templateModel.isSystem : false
                type: Kirigami.MessageType.Information
                text: i18n("This is a built-in template. Saving stores your own copy, and deleting that copy brings the built-in one back.")
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing

                Kirigami.Separator {
                    Kirigami.FormData.label: i18n("Template")
                    Kirigami.FormData.isSection: true
                }

                TextField {
                    id: descriptionField

                    Kirigami.FormData.label: i18n("Description:")
                    Accessible.name: i18n("Template description")
                    placeholderText: i18n("Optional description")
                    maximumLength: templatePanel.constants.descriptionMaxLength || 500
                    onEditingFinished: {
                        if (templatePanel.templateModel)
                            templatePanel.templateModel.description = text;
                    }
                }

                Kirigami.Separator {
                    Kirigami.FormData.label: i18n("Later columns")
                    Kirigami.FormData.isSection: true
                }

                ComboBox {
                    id: defaultWidthKindCombo

                    Kirigami.FormData.label: i18n("Default width:")
                    Accessible.name: i18n("Default column width")
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        {
                            "text": i18n("Fraction of the screen"),
                            "value": templatePanel.constants.kindProportion
                        },
                        {
                            "text": i18n("Fixed pixels"),
                            "value": templatePanel.constants.kindFixed
                        },
                        {
                            "text": i18n("The window decides"),
                            "value": templatePanel.constants.kindClientDecides
                        },
                        {
                            "text": i18n("Width preset"),
                            "value": templatePanel.constants.kindPreset
                        }
                    ]
                    onActivated: {
                        if (templatePanel.templateModel)
                            templatePanel.templateModel.defaultWidthKind = currentValue;
                    }
                }

                SpinBox {
                    id: defaultWidthValueSpin

                    readonly property bool isFixed: defaultWidthKindCombo.currentValue === templatePanel.constants.kindFixed

                    Kirigami.FormData.label: defaultWidthValueSpin.isFixed ? i18n("Width in pixels:") : i18n("Width:")
                    Accessible.name: defaultWidthValueSpin.isFixed ? i18n("Default column width in pixels") : i18n("Default column width")
                    visible: defaultWidthKindCombo.currentValue === templatePanel.constants.kindProportion || defaultWidthValueSpin.isFixed
                    from: defaultWidthValueSpin.isFixed ? Math.round(templatePanel.constants.fixedMin) : templatePanel.proportionMinPercent
                    to: defaultWidthValueSpin.isFixed ? Math.round(templatePanel.constants.fixedMax) : templatePanel.proportionMaxPercent
                    textFromValue: (value, locale) => defaultWidthValueSpin.isFixed ? String(value) : i18n("%1%", value)
                    valueFromText: (text, locale) => parseInt(text)
                    onValueModified: {
                        if (templatePanel.templateModel)
                            templatePanel.templateModel.defaultWidthValue = isFixed ? value : value / 100;
                    }
                }

                // The preset default picks the actual stop rather than a
                // bare index number: the combo lists the width presets in
                // percent, in the same order the cycling shortcut steps.
                ComboBox {
                    id: defaultPresetCombo

                    Kirigami.FormData.label: i18n("Start at preset:")
                    Accessible.name: i18n("Default width preset")
                    visible: defaultWidthKindCombo.currentValue === templatePanel.constants.kindPreset
                    enabled: templatePanel.presetWidthCount > 0
                    displayText: templatePanel.presetWidthCount === 0 ? i18n("No width presets yet") : currentText
                    model: templatePanel.templateModel ? (templatePanel.templateModel.presetWidths || []).map((value, index) => i18n("Preset %1 (%2%)", index + 1, Math.round(value * 100))) : []
                    onActivated: index => {
                        if (templatePanel.templateModel)
                            templatePanel.templateModel.defaultPresetIndex = index;
                    }
                }

                ComboBox {
                    id: defaultDisplayCombo

                    Kirigami.FormData.label: i18n("Show windows as:")
                    Accessible.name: i18n("Show windows as")
                    model: templatePanel.displayOptions
                    onActivated: {
                        if (templatePanel.templateModel)
                            templatePanel.templateModel.defaultDisplay = currentIndex;
                    }
                }

                Kirigami.Separator {
                    Kirigami.FormData.label: i18n("Size presets")
                    Kirigami.FormData.isSection: true
                }

                PresetChipEditor {
                    Kirigami.FormData.label: i18n("Widths:")
                    Kirigami.FormData.labelAlignment: Qt.AlignTop
                    Layout.fillWidth: true
                    accessibleLabel: i18n("Width presets")
                    values: templatePanel.templateModel ? templatePanel.templateModel.presetWidths : []
                    minPercent: templatePanel.proportionMinPercent
                    maxCount: templatePanel.constants.maxTemplateColumns || 16
                    dedupeEpsilon: templatePanel.constants.fractionDedupeEpsilon || 0.01
                    onEdited: newValues => {
                        if (templatePanel.templateModel)
                            templatePanel.templateModel.presetWidths = newValues;
                    }
                }

                PresetChipEditor {
                    Kirigami.FormData.label: i18n("Heights:")
                    Kirigami.FormData.labelAlignment: Qt.AlignTop
                    Layout.fillWidth: true
                    accessibleLabel: i18n("Height presets")
                    values: templatePanel.templateModel ? templatePanel.templateModel.presetHeights : []
                    minPercent: templatePanel.proportionMinPercent
                    maxCount: templatePanel.constants.maxTemplateColumns || 16
                    dedupeEpsilon: templatePanel.constants.fractionDedupeEpsilon || 0.01
                    onEdited: newValues => {
                        if (templatePanel.templateModel)
                            templatePanel.templateModel.presetHeights = newValues;
                    }
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: templatePanel.presetDefaultNeedsWidths
                type: Kirigami.MessageType.Warning
                text: i18n("The default width is set to a width preset, so this template needs at least one width preset. Without one it saves as a fraction of the screen instead.")
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: i18n("Presets are the sizes the width and height cycling shortcuts step through while this template is assigned.")
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
