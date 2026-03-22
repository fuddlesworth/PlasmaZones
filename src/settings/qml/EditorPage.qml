// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    // Bridge: combine Settings (appSettings) with SettingsController editor properties
    readonly property var settingsBridge: settingsController
    // Inline constants (from monolith Constants object)
    readonly property int sliderPreferredWidth: Kirigami.Units.gridUnit * 16
    readonly property int sliderValueLabelWidth: Kirigami.Units.gridUnit * 3

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // KEYBOARD SHORTCUTS CARD
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: shortcutsCard.implicitHeight

            SettingsCard {
                id: shortcutsCard

                anchors.fill: parent
                headerText: i18n("Keyboard Shortcuts")
                collapsible: true

                contentItem: Kirigami.FormLayout {
                    ShortcutCaptureField {
                        id: editorDuplicateShortcutField

                        formLabel: i18n("Duplicate zone:")
                        keySequence: settingsController.editorDuplicateShortcut
                        placeholderText: "Ctrl+D"
                        tooltipText: i18n("Keyboard shortcut to duplicate the selected zone. Click to capture.")
                        onKeySequenceModified: (seq) => {
                            settingsController.editorDuplicateShortcut = seq;
                        }
                    }

                    ShortcutCaptureField {
                        id: editorSplitHorizontalShortcutField

                        formLabel: i18n("Split horizontally:")
                        keySequence: settingsController.editorSplitHorizontalShortcut
                        placeholderText: "Ctrl+Shift+H"
                        tooltipText: i18n("Keyboard shortcut to split selected zone horizontally. Click to capture.")
                        onKeySequenceModified: (seq) => {
                            settingsController.editorSplitHorizontalShortcut = seq;
                        }
                    }

                    ShortcutCaptureField {
                        id: editorSplitVerticalShortcutField

                        formLabel: i18n("Split vertically:")
                        keySequence: settingsController.editorSplitVerticalShortcut
                        placeholderText: "Ctrl+Alt+V"
                        tooltipText: i18n("Keyboard shortcut to split selected zone vertically. Click to capture.")
                        onKeySequenceModified: (seq) => {
                            settingsController.editorSplitVerticalShortcut = seq;
                        }
                    }

                    ShortcutCaptureField {
                        id: editorFillShortcutField

                        formLabel: i18n("Fill space:")
                        keySequence: settingsController.editorFillShortcut
                        placeholderText: "Ctrl+Shift+F"
                        tooltipText: i18n("Keyboard shortcut to expand selected zone to fill available space. Click to capture.")
                        onKeySequenceModified: (seq) => {
                            settingsController.editorFillShortcut = seq;
                        }
                    }

                    Button {
                        Kirigami.FormData.label: i18n("Reset:")
                        text: i18n("Reset to defaults")
                        icon.name: "edit-reset"
                        onClicked: settingsController.resetEditorDefaults()
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Reset all editor shortcuts to their default values")
                    }

                }

            }

        }

        // =================================================================
        // SNAPPING CARD
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: snappingCard.implicitHeight

            SettingsCard {
                id: snappingCard

                anchors.fill: parent
                headerText: i18n("Snapping")
                collapsible: true

                contentItem: Kirigami.FormLayout {
                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Snap Behavior")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Grid snapping:")
                        text: i18n("Enable grid snapping")
                        checked: settingsController.editorGridSnappingEnabled
                        onToggled: settingsController.editorGridSnappingEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Snap zones to a grid while dragging or resizing")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Edge snapping:")
                        text: i18n("Enable edge snapping")
                        checked: settingsController.editorEdgeSnappingEnabled
                        onToggled: settingsController.editorEdgeSnappingEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Snap zones to edges of other zones while dragging or resizing")
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Grid Intervals")
                    }

                    SettingsSlider {
                        id: snapIntervalXSlider

                        formLabel: i18n("Grid interval X:")
                        from: 0.01
                        to: 0.5
                        stepSize: 0.01
                        value: settingsController.editorSnapIntervalX
                        formatValue: function(v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: (value) => {
                            return settingsController.editorSnapIntervalX = value;
                        }
                    }

                    SettingsSlider {
                        id: snapIntervalYSlider

                        formLabel: i18n("Grid interval Y:")
                        from: 0.01
                        to: 0.5
                        stepSize: 0.01
                        value: settingsController.editorSnapIntervalY
                        formatValue: function(v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: (value) => {
                            return settingsController.editorSnapIntervalY = value;
                        }
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Modifier")
                    }

                    ModifierComboBox {
                        formLabel: i18n("Override modifier:")
                        modifierValue: root.settingsBridge.editorSnapOverrideModifier
                        tooltipText: i18n("Hold this modifier to temporarily override snap behavior")
                        onModifierSelected: (value) => {
                            root.settingsBridge.editorSnapOverrideModifier = value;
                        }
                    }

                }

            }

        }

        // =================================================================
        // FILL ON DROP CARD
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: fillOnDropCard.implicitHeight

            SettingsCard {
                id: fillOnDropCard

                anchors.fill: parent
                headerText: i18n("Fill on Drop")

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        id: fillOnDropEnabledCheck

                        Kirigami.FormData.label: i18n("Enable:")
                        text: i18n("Fill zone on drop with modifier key")
                        checked: root.settingsBridge.fillOnDropEnabled
                        onToggled: root.settingsBridge.fillOnDropEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, holding the modifier key while dropping a zone expands it to fill available space")
                    }

                    ModifierComboBox {
                        formLabel: i18n("Modifier:")
                        enabled: fillOnDropEnabledCheck.checked
                        modifierValue: root.settingsBridge.fillOnDropModifier
                        tooltipText: i18n("Hold this modifier while dropping to fill available space")
                        onModifierSelected: (value) => {
                            root.settingsBridge.fillOnDropModifier = value;
                        }
                    }

                }

            }

        }

    }

    // Keep inputs in sync with settingsController properties
    Connections {
        function onEditorDuplicateShortcutChanged() {
            if (!editorDuplicateShortcutField.capturing)
                editorDuplicateShortcutField.keySequence = settingsController.editorDuplicateShortcut;

        }

        function onEditorSplitHorizontalShortcutChanged() {
            if (!editorSplitHorizontalShortcutField.capturing)
                editorSplitHorizontalShortcutField.keySequence = settingsController.editorSplitHorizontalShortcut;

        }

        function onEditorSplitVerticalShortcutChanged() {
            if (!editorSplitVerticalShortcutField.capturing)
                editorSplitVerticalShortcutField.keySequence = settingsController.editorSplitVerticalShortcut;

        }

        function onEditorFillShortcutChanged() {
            if (!editorFillShortcutField.capturing)
                editorFillShortcutField.keySequence = settingsController.editorFillShortcut;

        }

        function onEditorSnapIntervalXChanged() {
            if (!snapIntervalXSlider.slider.pressed)
                snapIntervalXSlider.value = settingsController.editorSnapIntervalX;

        }

        function onEditorSnapIntervalYChanged() {
            if (!snapIntervalYSlider.slider.pressed)
                snapIntervalYSlider.value = settingsController.editorSnapIntervalY;

        }

        target: settingsController
    }

}
