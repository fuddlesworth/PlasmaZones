// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Editor tab - Keyboard shortcuts and snapping settings for the layout editor
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    clip: true
    contentWidth: availableWidth

    Kirigami.FormLayout {
        width: parent.width

        // Editor shortcuts section
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Keyboard Shortcuts")
        }

        KeySequenceInput {
            id: editorDuplicateShortcutField
            Kirigami.FormData.label: i18n("Duplicate zone:")
            keySequence: kcm.editorDuplicateShortcut
            onKeySequenceModified: (sequence) => {
                kcm.editorDuplicateShortcut = sequence
            }
            ToolTip.visible: hovered
            ToolTip.text: i18n("Keyboard shortcut to duplicate the selected zone. Click and press keys.")
        }

        KeySequenceInput {
            id: editorSplitHorizontalShortcutField
            Kirigami.FormData.label: i18n("Split horizontally:")
            keySequence: kcm.editorSplitHorizontalShortcut
            onKeySequenceModified: (sequence) => {
                kcm.editorSplitHorizontalShortcut = sequence
            }
            ToolTip.visible: hovered
            ToolTip.text: i18n("Keyboard shortcut to split selected zone horizontally. Click and press keys.")
        }

        KeySequenceInput {
            id: editorSplitVerticalShortcutField
            Kirigami.FormData.label: i18n("Split vertically:")
            keySequence: kcm.editorSplitVerticalShortcut
            onKeySequenceModified: (sequence) => {
                kcm.editorSplitVerticalShortcut = sequence
            }
            ToolTip.visible: hovered
            ToolTip.text: i18n("Keyboard shortcut to split selected zone vertically. Click and press keys.")
        }

        KeySequenceInput {
            id: editorFillShortcutField
            Kirigami.FormData.label: i18n("Fill space:")
            keySequence: kcm.editorFillShortcut
            onKeySequenceModified: (sequence) => {
                kcm.editorFillShortcut = sequence
            }
            ToolTip.visible: hovered
            ToolTip.text: i18n("Keyboard shortcut to expand selected zone to fill available space. Click and press keys.")
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Reset shortcuts:")
            spacing: Kirigami.Units.smallSpacing

            Button {
                text: i18n("Reset to Defaults")
                icon.name: "edit-reset"
                onClicked: {
                    kcm.resetEditorShortcuts()
                }
                ToolTip.visible: hovered
                ToolTip.text: i18n("Reset all editor shortcuts to their default values")
            }
        }

        // Editor snapping section
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Snapping")
        }

        CheckBox {
            Kirigami.FormData.label: i18n("Grid snapping:")
            text: i18n("Enable grid snapping")
            checked: kcm.editorGridSnappingEnabled
            onToggled: kcm.editorGridSnappingEnabled = checked
            ToolTip.visible: hovered
            ToolTip.text: i18n("Snap zones to a grid while dragging or resizing")
        }

        CheckBox {
            Kirigami.FormData.label: i18n("Edge snapping:")
            text: i18n("Enable edge snapping")
            checked: kcm.editorEdgeSnappingEnabled
            onToggled: kcm.editorEdgeSnappingEnabled = checked
            ToolTip.visible: hovered
            ToolTip.text: i18n("Snap zones to edges of other zones while dragging or resizing")
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Grid interval X:")
            spacing: Kirigami.Units.smallSpacing

            Slider {
                id: snapIntervalXSlider
                Layout.preferredWidth: root.constants.sliderPreferredWidth
                from: 0.01
                to: 0.5
                stepSize: 0.01
                value: kcm.editorSnapIntervalX
                onMoved: kcm.editorSnapIntervalX = value
            }

            Label {
                text: Math.round(snapIntervalXSlider.value * 100) + "%"
                Layout.preferredWidth: root.constants.sliderValueLabelWidth
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Grid interval Y:")
            spacing: Kirigami.Units.smallSpacing

            Slider {
                id: snapIntervalYSlider
                Layout.preferredWidth: root.constants.sliderPreferredWidth
                from: 0.01
                to: 0.5
                stepSize: 0.01
                value: kcm.editorSnapIntervalY
                onMoved: kcm.editorSnapIntervalY = value
            }

            Label {
                text: Math.round(snapIntervalYSlider.value * 100) + "%"
                Layout.preferredWidth: root.constants.sliderValueLabelWidth
            }
        }

        ModifierCheckBoxes {
            id: snapOverrideModifiers
            Kirigami.FormData.label: i18n("Snap override modifiers:")
            modifierValue: kcm.editorSnapOverrideModifier
            tooltipEnabled: true
            onValueModified: (value) => {
                kcm.editorSnapOverrideModifier = value
            }
        }

        // Fill on Drop section
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Fill on Drop")
        }

        CheckBox {
            id: fillOnDropEnabledCheck
            Kirigami.FormData.label: i18n("Enable:")
            text: i18n("Fill zone on drop with modifier key")
            checked: kcm.fillOnDropEnabled
            onToggled: kcm.fillOnDropEnabled = checked
            ToolTip.visible: hovered
            ToolTip.text: i18n("When enabled, holding the modifier key while dropping a zone expands it to fill available space")
        }

        ModifierCheckBoxes {
            id: fillOnDropModifiers
            Kirigami.FormData.label: i18n("Fill on drop modifiers:")
            enabled: fillOnDropEnabledCheck.checked
            modifierValue: kcm.fillOnDropModifier
            tooltipEnabled: true
            onValueModified: (value) => {
                kcm.fillOnDropModifier = value
            }
        }
    }

    // Keep inputs in sync with KCM properties
    Connections {
        target: kcm
        function onEditorDuplicateShortcutChanged() {
            if (!editorDuplicateShortcutField.capturing) {
                editorDuplicateShortcutField.keySequence = kcm.editorDuplicateShortcut
            }
        }
        function onEditorSplitHorizontalShortcutChanged() {
            if (!editorSplitHorizontalShortcutField.capturing) {
                editorSplitHorizontalShortcutField.keySequence = kcm.editorSplitHorizontalShortcut
            }
        }
        function onEditorSplitVerticalShortcutChanged() {
            if (!editorSplitVerticalShortcutField.capturing) {
                editorSplitVerticalShortcutField.keySequence = kcm.editorSplitVerticalShortcut
            }
        }
        function onEditorFillShortcutChanged() {
            if (!editorFillShortcutField.capturing) {
                editorFillShortcutField.keySequence = kcm.editorFillShortcut
            }
        }
        function onEditorSnapIntervalXChanged() {
            if (!snapIntervalXSlider.pressed) {
                snapIntervalXSlider.value = kcm.editorSnapIntervalX
            }
        }
        function onEditorSnapIntervalYChanged() {
            if (!snapIntervalYSlider.pressed) {
                snapIntervalYSlider.value = kcm.editorSnapIntervalY
            }
        }
    }
}
