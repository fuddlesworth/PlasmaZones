// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Editor tab - Keyboard shortcuts and snapping settings for the layout editor
 *
 * Uses Card-based layout for consistency with other tabs.
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ═══════════════════════════════════════════════════════════════════════
        // KEYBOARD SHORTCUTS CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: shortcutsCard.implicitHeight

            Kirigami.Card {
                id: shortcutsCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Keyboard Shortcuts")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    PlasmaZonesKeySequenceInput {
                        id: editorDuplicateShortcutField
                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Duplicate zone:")
                        keySequence: kcm.editorDuplicateShortcut
                        defaultKeySequence: kcm.defaultEditorDuplicateShortcut
                        onKeySequenceModified: (sequence) => {
                            kcm.editorDuplicateShortcut = sequence
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Keyboard shortcut to duplicate the selected zone")
                    }

                    PlasmaZonesKeySequenceInput {
                        id: editorSplitHorizontalShortcutField
                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Split horizontally:")
                        keySequence: kcm.editorSplitHorizontalShortcut
                        defaultKeySequence: kcm.defaultEditorSplitHorizontalShortcut
                        onKeySequenceModified: (sequence) => {
                            kcm.editorSplitHorizontalShortcut = sequence
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Keyboard shortcut to split selected zone horizontally")
                    }

                    PlasmaZonesKeySequenceInput {
                        id: editorSplitVerticalShortcutField
                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Split vertically:")
                        keySequence: kcm.editorSplitVerticalShortcut
                        defaultKeySequence: kcm.defaultEditorSplitVerticalShortcut
                        onKeySequenceModified: (sequence) => {
                            kcm.editorSplitVerticalShortcut = sequence
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Keyboard shortcut to split selected zone vertically")
                    }

                    PlasmaZonesKeySequenceInput {
                        id: editorFillShortcutField
                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Fill space:")
                        keySequence: kcm.editorFillShortcut
                        defaultKeySequence: kcm.defaultEditorFillShortcut
                        onKeySequenceModified: (sequence) => {
                            kcm.editorFillShortcut = sequence
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Keyboard shortcut to expand selected zone to fill available space")
                    }

                    Button {
                        Kirigami.FormData.label: i18n("Reset:")
                        text: i18n("Reset to Defaults")
                        icon.name: "edit-reset"
                        onClicked: kcm.resetEditorShortcuts()
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Reset all editor shortcuts to their default values")
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // SNAPPING CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: snappingCard.implicitHeight

            Kirigami.Card {
                id: snappingCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Snapping")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
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

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
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

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                    }

                    ModifierAndMouseCheckBoxes {
                        id: snapOverrideInput
                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Override modifier:")
                        acceptMode: ModifierAndMouseCheckBoxes.acceptModeMetaOnly
                        modifierValue: kcm.editorSnapOverrideModifier
                        defaultModifierValue: kcm.defaultEditorSnapOverrideModifier
                        tooltipEnabled: true
                        onValueModified: (value) => {
                            kcm.editorSnapOverrideModifier = value
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // FILL ON DROP CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: fillOnDropCard.implicitHeight

            Kirigami.Card {
                id: fillOnDropCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Fill on Drop")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        id: fillOnDropEnabledCheck
                        Kirigami.FormData.label: i18n("Enable:")
                        text: i18n("Fill zone on drop with modifier key")
                        checked: kcm.fillOnDropEnabled
                        onToggled: kcm.fillOnDropEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, holding the modifier key while dropping a zone expands it to fill available space")
                    }

                    ModifierAndMouseCheckBoxes {
                        id: fillOnDropInput
                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Modifier:")
                        acceptMode: ModifierAndMouseCheckBoxes.acceptModeMetaOnly
                        enabled: fillOnDropEnabledCheck.checked
                        modifierValue: kcm.fillOnDropModifier
                        defaultModifierValue: kcm.defaultFillOnDropModifier
                        tooltipEnabled: true
                        onValueModified: (value) => {
                            kcm.fillOnDropModifier = value
                        }
                    }
                }
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
