// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    // Page-scoped Q_PROPERTY surface for the Editor page lives on the
    // sub-controller; SettingsController exposes it as a child QObject.
    readonly property var settingsBridge: settingsController.editorPage
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

                            keySequence: root.settingsBridge.duplicateShortcut
                            placeholderText: "Ctrl+D"
                            onKeySequenceModified: (seq) => {
                                root.settingsBridge.duplicateShortcut = seq;
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

                            keySequence: root.settingsBridge.splitHorizontalShortcut
                            placeholderText: "Ctrl+Shift+H"
                            onKeySequenceModified: (seq) => {
                                root.settingsBridge.splitHorizontalShortcut = seq;
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

                            keySequence: root.settingsBridge.splitVerticalShortcut
                            placeholderText: "Ctrl+Alt+V"
                            onKeySequenceModified: (seq) => {
                                root.settingsBridge.splitVerticalShortcut = seq;
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

                            keySequence: root.settingsBridge.fillShortcut
                            placeholderText: "Ctrl+Shift+F"
                            onKeySequenceModified: (seq) => {
                                root.settingsBridge.fillShortcut = seq;
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
                            onClicked: root.settingsBridge.resetDefaults()
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
                            checked: root.settingsBridge.gridSnappingEnabled
                            accessibleName: i18n("Enable grid snapping")
                            onToggled: function(newValue) {
                                root.settingsBridge.gridSnappingEnabled = newValue;
                            }
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Edge snapping")
                        description: i18n("Snap zones to edges of neighboring zones")

                        SettingsSwitch {
                            checked: root.settingsBridge.edgeSnappingEnabled
                            accessibleName: i18n("Enable edge snapping")
                            onToggled: function(newValue) {
                                root.settingsBridge.edgeSnappingEnabled = newValue;
                            }
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
                            value: root.settingsBridge.snapIntervalX
                            formatValue: function(v) {
                                return Math.round(v * 100) + "%";
                            }
                            onMoved: (value) => {
                                return root.settingsBridge.snapIntervalX = value;
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
                            value: root.settingsBridge.snapIntervalY
                            formatValue: function(v) {
                                return Math.round(v * 100) + "%";
                            }
                            onMoved: (value) => {
                                return root.settingsBridge.snapIntervalY = value;
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
                            modifierValue: root.settingsBridge.snapOverrideModifier
                            onModifierSelected: (value) => {
                                root.settingsBridge.snapOverrideModifier = value;
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

    // Keep inputs in sync with editorPage sub-controller properties
    Connections {
        function onDuplicateShortcutChanged() {
            if (!editorDuplicateShortcutField.capturing)
                editorDuplicateShortcutField.keySequence = root.settingsBridge.duplicateShortcut;

        }

        function onSplitHorizontalShortcutChanged() {
            if (!editorSplitHorizontalShortcutField.capturing)
                editorSplitHorizontalShortcutField.keySequence = root.settingsBridge.splitHorizontalShortcut;

        }

        function onSplitVerticalShortcutChanged() {
            if (!editorSplitVerticalShortcutField.capturing)
                editorSplitVerticalShortcutField.keySequence = root.settingsBridge.splitVerticalShortcut;

        }

        function onFillShortcutChanged() {
            if (!editorFillShortcutField.capturing)
                editorFillShortcutField.keySequence = root.settingsBridge.fillShortcut;

        }

        function onSnapIntervalXChanged() {
            if (!snapIntervalXSlider.slider.pressed)
                snapIntervalXSlider.value = root.settingsBridge.snapIntervalX;

        }

        function onSnapIntervalYChanged() {
            if (!snapIntervalYSlider.slider.pressed)
                snapIntervalYSlider.value = root.settingsBridge.snapIntervalY;

        }

        target: root.settingsBridge
    }

}
