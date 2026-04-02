// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsCard {
    id: root

    required property int gapMax
    required property int gapMin
    required property int innerGapValue
    required property int outerGapValue
    required property bool usePerSideOuterGap
    required property bool smartGapsValue
    required property int outerGapTopValue
    required property int outerGapBottomValue
    required property int outerGapLeftValue
    required property int outerGapRightValue

    signal innerGapModified(int value)
    signal outerGapModified(int value)
    signal usePerSideOuterGapToggled(bool checked)
    signal outerGapTopModified(int value)
    signal outerGapBottomModified(int value)
    signal outerGapLeftModified(int value)
    signal outerGapRightModified(int value)
    signal smartGapsToggled(bool checked)

    headerText: i18n("Gaps")
    collapsible: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsRow {
            title: i18n("Inner gap")
            description: i18n("Space between tiled windows")

            SettingsSpinBox {
                from: root.gapMin
                to: root.gapMax
                value: root.innerGapValue
                onValueModified: (value) => {
                    return root.innerGapModified(value);
                }
            }

        }

        SettingsSeparator {
        }

        SettingsRow {
            visible: !tilePerSideSwitch.checked
            title: i18n("Outer gap")
            description: i18n("Space from screen edges to tiled windows")

            SpinBox {
                id: outerGapSpinBox

                from: root.gapMin
                to: root.gapMax
                onValueModified: root.outerGapModified(value)
                Accessible.name: i18n("Outer gap")

                Binding on value {
                    value: root.outerGapValue
                    when: !outerGapSpinBox.activeFocus
                    restoreMode: Binding.RestoreNone
                }

            }

        }

        SettingsRow {
            title: i18n("Per-side outer gaps")
            description: tilePerSideSwitch.checked ? i18n("Set different gap sizes for each screen edge") : i18n("Use a single outer gap value for all edges")

            SettingsSwitch {
                id: tilePerSideSwitch

                checked: root.usePerSideOuterGap
                accessibleName: i18n("Set gaps per side")
                onToggled: function(newValue) {
                    root.usePerSideOuterGapToggled(newValue);
                }
            }

        }

        // Per-side gap grid (only when per-side is enabled)
        GridLayout {
            visible: tilePerSideSwitch.checked
            Layout.alignment: Qt.AlignRight
            Layout.rightMargin: Kirigami.Units.largeSpacing
            columns: 4
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.smallSpacing

            Label {
                text: i18n("Top")
            }

            SpinBox {
                id: topGapSpinBox

                from: root.gapMin
                to: root.gapMax
                onValueModified: root.outerGapTopModified(value)
                Accessible.name: i18nc("@label", "Top gap")

                Binding on value {
                    value: root.outerGapTopValue
                    when: !topGapSpinBox.activeFocus
                    restoreMode: Binding.RestoreNone
                }

            }

            Label {
                text: i18n("Bottom")
            }

            SpinBox {
                id: bottomGapSpinBox

                from: root.gapMin
                to: root.gapMax
                onValueModified: root.outerGapBottomModified(value)
                Accessible.name: i18nc("@label", "Bottom gap")

                Binding on value {
                    value: root.outerGapBottomValue
                    when: !bottomGapSpinBox.activeFocus
                    restoreMode: Binding.RestoreNone
                }

            }

            Label {
                text: i18n("Left")
            }

            SpinBox {
                id: leftGapSpinBox

                from: root.gapMin
                to: root.gapMax
                onValueModified: root.outerGapLeftModified(value)
                Accessible.name: i18nc("@label", "Left gap")

                Binding on value {
                    value: root.outerGapLeftValue
                    when: !leftGapSpinBox.activeFocus
                    restoreMode: Binding.RestoreNone
                }

            }

            Label {
                text: i18n("Right")
            }

            SpinBox {
                id: rightGapSpinBox

                from: root.gapMin
                to: root.gapMax
                onValueModified: root.outerGapRightModified(value)
                Accessible.name: i18nc("@label", "Right gap")

                Binding on value {
                    value: root.outerGapRightValue
                    when: !rightGapSpinBox.activeFocus
                    restoreMode: Binding.RestoreNone
                }

            }

        }

        SettingsSeparator {
        }

        SettingsRow {
            title: i18n("Smart gaps")
            description: i18n("Remove all gaps when only one window is tiled")

            SettingsSwitch {
                checked: root.smartGapsValue
                accessibleName: i18n("Smart gaps")
                onToggled: function(newValue) {
                    root.smartGapsToggled(newValue);
                }
            }

        }

    }

}
