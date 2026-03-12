// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: root

    // Capture the context property so child components can access it via root.kcmModule
    readonly property var kcmModule: kcm
    // Inline constants (from monolith Constants object)
    readonly property int sliderPreferredWidth: 200
    readonly property int sliderValueLabelWidth: 40

    topPadding: Kirigami.Units.largeSpacing
    bottomPadding: Kirigami.Units.largeSpacing
    leftPadding: Kirigami.Units.largeSpacing
    rightPadding: Kirigami.Units.largeSpacing

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ═══════════════════════════════════════════════════════════════════
        // KEYBOARD SHORTCUTS CARD
        // ═══════════════════════════════════════════════════════════════════
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
                    KeySequenceInput {
                        id: editorDuplicateShortcutField

                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Duplicate zone:")
                        keySequence: root.kcmModule.editorDuplicateShortcut
                        defaultKeySequence: root.kcmModule.defaultEditorDuplicateShortcut
                        onKeySequenceModified: (sequence) => {
                            root.kcmModule.editorDuplicateShortcut = sequence;
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Keyboard shortcut to duplicate the selected zone")
                    }

                    KeySequenceInput {
                        id: editorSplitHorizontalShortcutField

                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Split horizontally:")
                        keySequence: root.kcmModule.editorSplitHorizontalShortcut
                        defaultKeySequence: root.kcmModule.defaultEditorSplitHorizontalShortcut
                        onKeySequenceModified: (sequence) => {
                            root.kcmModule.editorSplitHorizontalShortcut = sequence;
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Keyboard shortcut to split selected zone horizontally")
                    }

                    KeySequenceInput {
                        id: editorSplitVerticalShortcutField

                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Split vertically:")
                        keySequence: root.kcmModule.editorSplitVerticalShortcut
                        defaultKeySequence: root.kcmModule.defaultEditorSplitVerticalShortcut
                        onKeySequenceModified: (sequence) => {
                            root.kcmModule.editorSplitVerticalShortcut = sequence;
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Keyboard shortcut to split selected zone vertically")
                    }

                    KeySequenceInput {
                        id: editorFillShortcutField

                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Fill space:")
                        keySequence: root.kcmModule.editorFillShortcut
                        defaultKeySequence: root.kcmModule.defaultEditorFillShortcut
                        onKeySequenceModified: (sequence) => {
                            root.kcmModule.editorFillShortcut = sequence;
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Keyboard shortcut to expand selected zone to fill available space")
                    }

                    Button {
                        Kirigami.FormData.label: i18n("Reset:")
                        text: i18n("Reset to defaults")
                        icon.name: "edit-reset"
                        onClicked: root.kcmModule.resetEditorShortcuts()
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Reset all editor shortcuts to their default values")
                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════════
        // SNAPPING CARD
        // ═══════════════════════════════════════════════════════════════════
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
                        checked: root.kcmModule.editorGridSnappingEnabled
                        onToggled: root.kcmModule.editorGridSnappingEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Snap zones to a grid while dragging or resizing")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Edge snapping:")
                        text: i18n("Enable edge snapping")
                        checked: root.kcmModule.editorEdgeSnappingEnabled
                        onToggled: root.kcmModule.editorEdgeSnappingEnabled = checked
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

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 0.01
                            to: 0.5
                            stepSize: 0.01
                            value: root.kcmModule.editorSnapIntervalX
                            onMoved: root.kcmModule.editorSnapIntervalX = value
                        }

                        Label {
                            text: Math.round(snapIntervalXSlider.value * 100) + "%"
                            Layout.preferredWidth: root.sliderValueLabelWidth
                        }

                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Grid interval Y:")
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: snapIntervalYSlider

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 0.01
                            to: 0.5
                            stepSize: 0.01
                            value: root.kcmModule.editorSnapIntervalY
                            onMoved: root.kcmModule.editorSnapIntervalY = value
                        }

                        Label {
                            text: Math.round(snapIntervalYSlider.value * 100) + "%"
                            Layout.preferredWidth: root.sliderValueLabelWidth
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
                        modifierValue: root.kcmModule.editorSnapOverrideModifier
                        defaultModifierValue: root.kcmModule.defaultEditorSnapOverrideModifier
                        tooltipEnabled: true
                        onValueModified: (value) => {
                            root.kcmModule.editorSnapOverrideModifier = value;
                        }
                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════════
        // FILL ON DROP CARD
        // ═══════════════════════════════════════════════════════════════════
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
                        checked: root.kcmModule.fillOnDropEnabled
                        onToggled: root.kcmModule.fillOnDropEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, holding the modifier key while dropping a zone expands it to fill available space")
                    }

                    ModifierAndMouseCheckBoxes {
                        id: fillOnDropInput

                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Modifier:")
                        acceptMode: ModifierAndMouseCheckBoxes.acceptModeMetaOnly
                        enabled: fillOnDropEnabledCheck.checked
                        modifierValue: root.kcmModule.fillOnDropModifier
                        defaultModifierValue: root.kcmModule.defaultFillOnDropModifier
                        tooltipEnabled: true
                        onValueModified: (value) => {
                            root.kcmModule.fillOnDropModifier = value;
                        }
                    }

                }

            }

        }

    }

    // Keep inputs in sync with KCM properties
    Connections {
        function onEditorDuplicateShortcutChanged() {
            if (!editorDuplicateShortcutField.capturing)
                editorDuplicateShortcutField.keySequence = root.kcmModule.editorDuplicateShortcut;

        }

        function onEditorSplitHorizontalShortcutChanged() {
            if (!editorSplitHorizontalShortcutField.capturing)
                editorSplitHorizontalShortcutField.keySequence = root.kcmModule.editorSplitHorizontalShortcut;

        }

        function onEditorSplitVerticalShortcutChanged() {
            if (!editorSplitVerticalShortcutField.capturing)
                editorSplitVerticalShortcutField.keySequence = root.kcmModule.editorSplitVerticalShortcut;

        }

        function onEditorFillShortcutChanged() {
            if (!editorFillShortcutField.capturing)
                editorFillShortcutField.keySequence = root.kcmModule.editorFillShortcut;

        }

        function onEditorSnapIntervalXChanged() {
            if (!snapIntervalXSlider.pressed)
                snapIntervalXSlider.value = root.kcmModule.editorSnapIntervalX;

        }

        function onEditorSnapIntervalYChanged() {
            if (!snapIntervalYSlider.pressed)
                snapIntervalYSlider.value = root.kcmModule.editorSnapIntervalY;

        }

        target: root.kcmModule
    }

}
