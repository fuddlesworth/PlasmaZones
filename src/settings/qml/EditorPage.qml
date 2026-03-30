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

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Duplicate zone")
                        description: i18n("Clone the currently selected zone")

                        ShortcutCaptureField {
                            id: editorDuplicateShortcutField

                            keySequence: settingsController.editorDuplicateShortcut
                            placeholderText: "Ctrl+D"
                            onKeySequenceModified: (seq) => {
                                settingsController.editorDuplicateShortcut = seq;
                            }
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Split horizontally")
                        description: i18n("Divide selected zone into left and right halves")

                        ShortcutCaptureField {
                            id: editorSplitHorizontalShortcutField

                            keySequence: settingsController.editorSplitHorizontalShortcut
                            placeholderText: "Ctrl+Shift+H"
                            onKeySequenceModified: (seq) => {
                                settingsController.editorSplitHorizontalShortcut = seq;
                            }
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Split vertically")
                        description: i18n("Divide selected zone into top and bottom halves")

                        ShortcutCaptureField {
                            id: editorSplitVerticalShortcutField

                            keySequence: settingsController.editorSplitVerticalShortcut
                            placeholderText: "Ctrl+Alt+V"
                            onKeySequenceModified: (seq) => {
                                settingsController.editorSplitVerticalShortcut = seq;
                            }
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Fill space")
                        description: i18n("Expand selected zone to fill surrounding empty area")

                        ShortcutCaptureField {
                            id: editorFillShortcutField

                            keySequence: settingsController.editorFillShortcut
                            placeholderText: "Ctrl+Shift+F"
                            onKeySequenceModified: (seq) => {
                                settingsController.editorFillShortcut = seq;
                            }
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Reset shortcuts")
                        description: i18n("Restore all editor shortcuts to their default values")

                        Button {
                            text: i18n("Reset to defaults")
                            icon.name: "edit-reset"
                            onClicked: settingsController.resetEditorDefaults()
                        }

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

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    // ── Snap Behavior ──────────────────────────────────────
                    SettingsRow {
                        title: i18n("Grid snapping")
                        description: i18n("Snap zones to a grid while dragging or resizing")

                        SettingsSwitch {
                            checked: settingsController.editorGridSnappingEnabled
                            accessibleName: i18n("Enable grid snapping")
                            onToggled: settingsController.editorGridSnappingEnabled = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Edge snapping")
                        description: i18n("Snap zones to edges of neighboring zones")

                        SettingsSwitch {
                            checked: settingsController.editorEdgeSnappingEnabled
                            accessibleName: i18n("Enable edge snapping")
                            onToggled: settingsController.editorEdgeSnappingEnabled = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    // ── Grid Intervals ─────────────────────────────────────
                    SettingsRow {
                        title: i18n("Grid interval X")
                        description: i18n("Horizontal grid spacing as percentage of screen width")

                        SettingsSlider {
                            id: snapIntervalXSlider

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

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Grid interval Y")
                        description: i18n("Vertical grid spacing as percentage of screen height")

                        SettingsSlider {
                            id: snapIntervalYSlider

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

                    }

                    SettingsSeparator {
                    }

                    // ── Modifier ───────────────────────────────────────────
                    SettingsRow {
                        title: i18n("Override modifier")
                        description: i18n("Hold this key to temporarily bypass snap behavior")

                        ModifierComboBox {
                            modifierValue: root.settingsBridge.editorSnapOverrideModifier
                            onModifierSelected: (value) => {
                                root.settingsBridge.editorSnapOverrideModifier = value;
                            }
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
                showToggle: true
                toggleChecked: root.settingsBridge.fillOnDropEnabled
                onToggleClicked: (checked) => {
                    return root.settingsBridge.fillOnDropEnabled = checked;
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Modifier key")
                        description: i18n("Hold this key while dropping a zone to expand it into available space")

                        ModifierComboBox {
                            modifierValue: root.settingsBridge.fillOnDropModifier
                            onModifierSelected: (value) => {
                                root.settingsBridge.fillOnDropModifier = value;
                            }
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
