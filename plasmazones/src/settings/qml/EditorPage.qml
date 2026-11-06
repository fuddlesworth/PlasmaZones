// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
    id: root

    // Set by PageHost before the page's bindings run (passesContext).
    required property var controller
    required property var settingsApp

    // Page-scoped Q_PROPERTY surface for the Editor page lives on the
    // sub-controller; SettingsController exposes it as a child QObject.
    readonly property var settingsBridge: root.controller

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
                            accessibleName: i18n("Duplicate zone")
                            keySequence: root.settingsBridge.duplicateShortcut
                            placeholderText: "Ctrl+D"
                            onKeySequenceModified: seq => {
                                root.settingsBridge.duplicateShortcut = seq;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Split horizontally")
                        description: i18n("Divide selected zone into left and right halves")

                        ShortcutCaptureField {
                            accessibleName: i18n("Split horizontally")
                            keySequence: root.settingsBridge.splitHorizontalShortcut
                            placeholderText: "Ctrl+Shift+H"
                            onKeySequenceModified: seq => {
                                root.settingsBridge.splitHorizontalShortcut = seq;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Split vertically")
                        description: i18n("Divide selected zone into top and bottom halves")

                        ShortcutCaptureField {
                            accessibleName: i18n("Split vertically")
                            keySequence: root.settingsBridge.splitVerticalShortcut
                            placeholderText: "Ctrl+Alt+V"
                            onKeySequenceModified: seq => {
                                root.settingsBridge.splitVerticalShortcut = seq;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Fill space")
                        description: i18n("Expand selected zone to fill surrounding empty area")

                        ShortcutCaptureField {
                            accessibleName: i18n("Fill space")
                            keySequence: root.settingsBridge.fillShortcut
                            placeholderText: "Ctrl+Shift+F"
                            onKeySequenceModified: seq => {
                                root.settingsBridge.fillShortcut = seq;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Reset shortcuts")
                        description: i18n("Restore all editor shortcuts to their default values")

                        Button {
                            text: i18n("Reset to defaults")
                            icon.name: "edit-reset"
                            onClicked: root.settingsBridge.resetShortcutDefaults()
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
                            onToggled: function (newValue) {
                                root.settingsBridge.gridSnappingEnabled = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Edge snapping")
                        description: i18n("Snap zones to edges of neighboring zones")

                        SettingsSwitch {
                            checked: root.settingsBridge.edgeSnappingEnabled
                            accessibleName: i18n("Enable edge snapping")
                            onToggled: function (newValue) {
                                root.settingsBridge.edgeSnappingEnabled = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    // ── Grid Intervals ─────────────────────────────────────
                    SettingsRow {
                        title: i18n("Grid interval X")
                        description: i18n("Horizontal grid spacing as percentage of screen width")

                        SettingsSlider {
                            accessibleName: i18n("Grid interval X")
                            // Percent units, not the stored fraction: a step of
                            // 1/100 lands on values like 0.35000000000000003,
                            // which never compares equal to a saved 0.35.
                            from: Math.round(root.settingsBridge.snapIntervalMin * 100)
                            to: Math.round(root.settingsBridge.snapIntervalMax * 100)
                            stepSize: 1
                            tickMarkStepSize: 10
                            value: Math.round(root.settingsBridge.snapIntervalX * 100)
                            formatValue: function (v) {
                                return i18nc("a whole-number percentage", "%1%", Math.round(v));
                            }
                            onMoved: value => {
                                root.settingsBridge.snapIntervalX = Math.round(value) / 100;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Grid interval Y")
                        description: i18n("Vertical grid spacing as percentage of screen height")

                        SettingsSlider {
                            accessibleName: i18n("Grid interval Y")
                            // Percent units, not the stored fraction: a step of
                            // 1/100 lands on values like 0.35000000000000003,
                            // which never compares equal to a saved 0.35.
                            from: Math.round(root.settingsBridge.snapIntervalMin * 100)
                            to: Math.round(root.settingsBridge.snapIntervalMax * 100)
                            stepSize: 1
                            tickMarkStepSize: 10
                            value: Math.round(root.settingsBridge.snapIntervalY * 100)
                            formatValue: function (v) {
                                return i18nc("a whole-number percentage", "%1%", Math.round(v));
                            }
                            onMoved: value => {
                                root.settingsBridge.snapIntervalY = Math.round(value) / 100;
                            }
                        }
                    }

                    SettingsSeparator {}

                    // ── Modifier ───────────────────────────────────────────
                    SettingsRow {
                        title: i18n("Override modifier")
                        description: i18n("Hold this key to temporarily bypass snap behavior")

                        ModifierComboBox {
                            accessibleName: i18n("Override modifier")
                            modifierValue: root.settingsBridge.snapOverrideModifier
                            onModifierSelected: value => {
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
                onToggleClicked: checked => {
                    return root.settingsBridge.fillOnDropEnabled = checked;
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Modifier key")
                        description: i18n("Hold this key while dropping a zone to expand it into available space")

                        ModifierComboBox {
                            accessibleName: i18n("Fill on drop modifier key")
                            modifierValue: root.settingsBridge.fillOnDropModifier
                            onModifierSelected: value => {
                                root.settingsBridge.fillOnDropModifier = value;
                            }
                        }
                    }
                }
            }
        }

        // =================================================================
        // APPEARANCE CARD
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: appearanceCard.implicitHeight

            SettingsCard {
                id: appearanceCard

                anchors.fill: parent
                headerText: i18n("Appearance")
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Background opacity")
                        searchAnchor: "editorBackgroundOpacity"
                        description: i18n("Lower values let more of the desktop show through behind the editor")

                        SettingsSlider {
                            accessibleName: i18n("Background opacity")
                            // Percent units for the same reason as the grid
                            // intervals, and it matters more here: the default,
                            // 0.7, is one of the fractions a 0.01 step misses.
                            from: Math.round(root.settingsBridge.backgroundOpacityMin * 100)
                            to: Math.round(root.settingsBridge.backgroundOpacityMax * 100)
                            stepSize: 1
                            tickMarkStepSize: 10
                            value: Math.round(root.settingsBridge.backgroundOpacity * 100)
                            formatValue: function (v) {
                                return i18nc("a whole-number percentage", "%1%", Math.round(v));
                            }
                            onMoved: value => {
                                root.settingsBridge.backgroundOpacity = Math.round(value) / 100;
                            }
                        }
                    }
                }
            }
        }
    }
}
