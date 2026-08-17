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

    // The || fallbacks mirror the C++ constants (ConfigDefaults proportion
    // bounds, MaxTemplateColumns, FractionDedupeEpsilon,
    // MaxTemplateDescriptionLength) and exist only for the teardown window
    // where templateModel goes null and `constants` reads as {} — keep them
    // in sync with EditorTemplateModel::scrollingConstants().
    readonly property var constants: templateModel ? templateModel.scrollingConstants() : ({})
    readonly property int proportionMinPercent: Math.round((constants.proportionMin || 0.05) * 100)
    readonly property int proportionMaxPercent: Math.round((constants.proportionMax || 1) * 100)
    // The chips commit every edit straight to the model, so the live stop
    // count reads from the model rather than from a half-typed text field.
    readonly property int presetWidthCount: templateModel ? (templateModel.presetWidths || []).length : 0
    // The null-model gate matters: with templateModel null both sides of the
    // kind comparison are undefined and would compare equal.
    readonly property bool presetDefaultNeedsWidths: templateModel !== null && defaultWidthKindCombo.currentValue === constants.kindPreset && presetWidthCount === 0

    function syncDefaults() {
        if (!templateModel)
            return;

        // Unknown kinds fall back to Proportion, the same coercion the
        // store's normalize() applies (coercing to Preset would hand a
        // template a concrete preset width its file never asked for).
        const kindIndex = defaultWidthKindCombo.indexOfValue(templateModel.defaultWidthKind);
        defaultWidthKindCombo.currentIndex = kindIndex >= 0 ? kindIndex : defaultWidthKindCombo.indexOfValue(constants.kindProportion);
        // Fixed stores a pixel count, the fraction kinds a 0..1 fraction the
        // spin shows as percent.
        const isFixed = templateModel.defaultWidthKind === constants.kindFixed;
        defaultWidthValueSpin.value = isFixed ? Math.round(templateModel.defaultWidthValue) : Math.round(templateModel.defaultWidthValue * 100);
        // Clamp the stored index against the live stop list so a shrunk
        // vocabulary never leaves the combo pointing past its model.
        defaultPresetCombo.currentIndex = Math.min(templateModel.defaultPresetIndex, templatePanel.presetWidthCount - 1);
        defaultDisplayCombo.currentIndex = defaultDisplayCombo.indexOfValue(templateModel.defaultDisplay === 1 ? 1 : 0);
    }

    function syncFromController() {
        if (!templateModel)
            return;

        if (!descriptionField.activeFocus)
            descriptionField.text = templateModel.description || "";
        syncDefaults();
    }

    // Sizing lives on the hosting Loader in EditorWindow.qml (the panel is
    // no longer a direct layout child); only visibility is mirrored here.
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
        // re-syncs the defaults too. Deferred a tick: this Connections
        // object connects to presetsChanged BEFORE the combo's model binding
        // (creation order), so an inline sync would write the index against
        // the OLD model and the rebuild would then reset it.
        function onPresetsChanged() {
            Qt.callLater(templatePanel.syncDefaults);
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
                text: i18nc("@info", "This is a built-in template. Saving stores your own copy, and deleting that copy brings the built-in one back.")
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing

                Kirigami.Separator {
                    Kirigami.FormData.label: i18nc("@title:group", "Template")
                    Kirigami.FormData.isSection: true
                }

                TextField {
                    id: descriptionField

                    Kirigami.FormData.label: i18nc("@label:textbox", "Description:")
                    Accessible.name: i18nc("@label:textbox", "Template description")
                    placeholderText: i18nc("@info:placeholder", "Optional description")
                    // Mirrors PlasmaZones::MaxTemplateDescriptionLength
                    // (core/types/constants.h); the model setter and the
                    // D-Bus boundary re-apply the same cap.
                    maximumLength: templatePanel.constants.descriptionMaxLength || 500
                    onTextChanged: {
                        // Focus-gated auto-commit, the PropertyPanel zone-name
                        // pattern: typing must reach the model without
                        // waiting for a focus change, or the description is
                        // the one edit Ctrl+S saves WITHOUT (the Save
                        // shortcut moves no focus) and the one edit that
                        // cannot re-enable a greyed Save button on its own.
                        if (activeFocus)
                            descriptionCommitTimer.restart();
                    }
                    onEditingFinished: {
                        descriptionCommitTimer.stop();
                        if (templatePanel.templateModel)
                            templatePanel.templateModel.description = text;
                    }

                    Timer {
                        id: descriptionCommitTimer

                        interval: 300
                        onTriggered: {
                            if (templatePanel.templateModel)
                                templatePanel.templateModel.description = descriptionField.text;
                        }
                    }
                }

                Kirigami.Separator {
                    Kirigami.FormData.label: i18nc("@title:group", "Later columns")
                    Kirigami.FormData.isSection: true
                }

                ComboBox {
                    id: defaultWidthKindCombo

                    Kirigami.FormData.label: i18nc("@label:listbox", "Default width:")
                    Accessible.name: i18nc("@label:listbox", "Default column width")
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        {
                            "text": i18nc("@item:inlistbox default width kind", "Proportion of the strip"),
                            "value": templatePanel.constants.kindProportion
                        },
                        {
                            "text": i18nc("@item:inlistbox default width kind", "Fixed pixels"),
                            "value": templatePanel.constants.kindFixed
                        },
                        {
                            "text": i18nc("@item:inlistbox default width kind", "The window decides"),
                            "value": templatePanel.constants.kindClientDecides
                        },
                        {
                            "text": i18nc("@item:inlistbox default width kind", "Width preset"),
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

                    readonly property bool isFixed: templatePanel.templateModel !== null && defaultWidthKindCombo.currentValue === templatePanel.constants.kindFixed

                    Kirigami.FormData.label: defaultWidthValueSpin.isFixed ? i18nc("@label:spinbox", "Width in pixels:") : i18nc("@label:spinbox", "Width:")
                    Accessible.name: defaultWidthValueSpin.isFixed ? i18nc("@label:spinbox", "Default column width in pixels") : i18nc("@label:spinbox", "Default column width")
                    visible: templatePanel.templateModel !== null && (defaultWidthKindCombo.currentValue === templatePanel.constants.kindProportion || defaultWidthValueSpin.isFixed)
                    // The || fallbacks mirror ConfigDefaults' fixed range, per
                    // the file's convention note at the top: a constants map
                    // missing a key must degrade to the shipped bound, not to
                    // a 0..0 spin.
                    from: defaultWidthValueSpin.isFixed ? Math.round(templatePanel.constants.fixedMin || 100) : templatePanel.proportionMinPercent
                    to: defaultWidthValueSpin.isFixed ? Math.round(templatePanel.constants.fixedMax || 10000) : templatePanel.proportionMaxPercent
                    textFromValue: (value, locale) => defaultWidthValueSpin.isFixed ? String(value) : i18nc("@info:spinbox width percentage", "%1%", value)
                    // Locale-aware on purpose: parseInt reads only ASCII
                    // digits and a locale that writes its numbers any other
                    // way would parse as NaN and poison the value. The percent
                    // sign textFromValue adds is stripped first, and text the
                    // locale cannot parse keeps the current value rather than
                    // committing NaN.
                    valueFromText: (text, locale) => {
                        const trimmed = text.replace(locale.percent, "").trim();
                        try {
                            return Math.round(Number.fromLocaleString(locale, trimmed));
                        } catch (e) {
                            return defaultWidthValueSpin.value;
                        }
                    }
                    onActiveFocusChanged: {
                        // A fresh focus session is a fresh undo gesture: the
                        // model merges consecutive value commits under one
                        // token, so bumping it here keeps one editing session
                        // one undo entry and separate sessions separate.
                        if (activeFocus && templatePanel.templateModel)
                            templatePanel.templateModel.beginWidthDrag();
                    }
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

                    Kirigami.FormData.label: i18nc("@label:listbox", "Start at preset:")
                    Accessible.name: i18nc("@label:listbox", "Default width preset")
                    visible: templatePanel.templateModel !== null && defaultWidthKindCombo.currentValue === templatePanel.constants.kindPreset
                    enabled: templatePanel.presetWidthCount > 0
                    displayText: templatePanel.presetWidthCount === 0 ? i18nc("@info:placeholder", "No width presets yet") : currentText
                    model: templatePanel.templateModel ? (templatePanel.templateModel.presetWidths || []).map((value, index) => i18nc("@item:inlistbox preset number and percentage", "Preset %1 (%2%)", index + 1, Math.round(value * 100))) : []
                    onActivated: index => {
                        if (templatePanel.templateModel)
                            templatePanel.templateModel.defaultPresetIndex = index;
                    }
                }

                ComboBox {
                    id: defaultDisplayCombo

                    Kirigami.FormData.label: i18nc("@label:listbox", "Show windows as:")
                    Accessible.name: i18nc("@label:listbox", "Show windows as")
                    textRole: "text"
                    valueRole: "value"
                    // Explicit wire values rather than bare indices, matching
                    // the kind combo: the ColumnDisplay vocabulary is 0
                    // Stacked, 1 Tabbed.
                    model: [
                        {
                            "text": i18nc("@item:inlistbox column display", "Stacked"),
                            "value": 0
                        },
                        {
                            "text": i18nc("@item:inlistbox column display", "Tabbed"),
                            "value": 1
                        }
                    ]
                    onActivated: {
                        if (templatePanel.templateModel)
                            templatePanel.templateModel.defaultDisplay = currentValue;
                    }
                }

                Kirigami.Separator {
                    Kirigami.FormData.label: i18nc("@title:group", "Size presets")
                    Kirigami.FormData.isSection: true
                }

                PresetChipEditor {
                    Kirigami.FormData.label: i18nc("@label", "Widths:")
                    Kirigami.FormData.labelAlignment: Qt.AlignTop
                    Layout.fillWidth: true
                    accessibleLabel: i18nc("@label", "Width presets")
                    values: templatePanel.templateModel ? templatePanel.templateModel.presetWidths : []
                    minPercent: templatePanel.proportionMinPercent
                    maxPercent: templatePanel.proportionMaxPercent
                    maxCount: templatePanel.constants.maxTemplateColumns || 16
                    dedupeEpsilon: templatePanel.constants.fractionDedupeEpsilon || 0.01
                    onEdited: newValues => {
                        if (templatePanel.templateModel)
                            templatePanel.templateModel.presetWidths = newValues;
                    }
                }

                PresetChipEditor {
                    Kirigami.FormData.label: i18nc("@label", "Heights:")
                    Kirigami.FormData.labelAlignment: Qt.AlignTop
                    Layout.fillWidth: true
                    accessibleLabel: i18nc("@label", "Height presets")
                    values: templatePanel.templateModel ? templatePanel.templateModel.presetHeights : []
                    minPercent: templatePanel.proportionMinPercent
                    maxPercent: templatePanel.proportionMaxPercent
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
                text: i18nc("@info", "The default width is set to a width preset, so this template needs at least one width preset. Without one it saves as a fraction of the strip instead.")
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: i18nc("@info", "Presets are the sizes the width and height cycling shortcuts step through while this template is assigned.")
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
