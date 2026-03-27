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

    contentItem: Kirigami.FormLayout {
        SettingsSpinBox {
            formLabel: i18n("Inner gap:")
            from: root.gapMin
            to: root.gapMax
            value: root.innerGapValue
            tooltipText: i18n("Gap between tiled windows")
            onValueModified: (value) => {
                return root.innerGapModified(value);
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Outer gap:")
            spacing: Kirigami.Units.smallSpacing

            SpinBox {
                id: outerGapSpinBox

                from: 0
                to: root.gapMax
                enabled: !tilePerSideCheck.checked
                onValueModified: root.outerGapModified(value)
                Accessible.name: i18n("Outer gap")
                ToolTip.visible: hovered
                ToolTip.text: i18n("Gap from screen edges")

                Binding on value {
                    value: root.outerGapValue
                    when: !outerGapSpinBox.activeFocus
                    restoreMode: Binding.RestoreNone
                }

            }

            Label {
                text: i18n("px")
                visible: !tilePerSideCheck.checked
            }

            CheckBox {
                id: tilePerSideCheck

                text: i18n("Set per side")
                checked: root.usePerSideOuterGap
                onToggled: root.usePerSideOuterGapToggled(checked)
                Accessible.name: i18n("Set gaps per side")
            }

        }

        GridLayout {
            Kirigami.FormData.label: i18n("Per-side gaps:")
            visible: tilePerSideCheck.checked
            columns: 6
            columnSpacing: Kirigami.Units.smallSpacing
            rowSpacing: Kirigami.Units.smallSpacing

            Label {
                text: i18n("Top:")
            }

            SpinBox {
                id: topGapSpinBox

                from: 0
                to: root.gapMax
                onValueModified: root.outerGapTopModified(value)
                Accessible.name: i18n("Top gap")

                Binding on value {
                    value: root.outerGapTopValue
                    when: !topGapSpinBox.activeFocus
                    restoreMode: Binding.RestoreNone
                }

            }

            Label {
                text: i18nc("@label", "px")
            }

            Label {
                text: i18n("Bottom:")
            }

            SpinBox {
                id: bottomGapSpinBox

                from: 0
                to: root.gapMax
                onValueModified: root.outerGapBottomModified(value)
                Accessible.name: i18n("Bottom gap")

                Binding on value {
                    value: root.outerGapBottomValue
                    when: !bottomGapSpinBox.activeFocus
                    restoreMode: Binding.RestoreNone
                }

            }

            Label {
                text: i18nc("@label", "px")
            }

            Label {
                text: i18n("Left:")
            }

            SpinBox {
                id: leftGapSpinBox

                from: 0
                to: root.gapMax
                onValueModified: root.outerGapLeftModified(value)
                Accessible.name: i18n("Left gap")

                Binding on value {
                    value: root.outerGapLeftValue
                    when: !leftGapSpinBox.activeFocus
                    restoreMode: Binding.RestoreNone
                }

            }

            Label {
                text: i18nc("@label", "px")
            }

            Label {
                text: i18n("Right:")
            }

            SpinBox {
                id: rightGapSpinBox

                from: 0
                to: root.gapMax
                onValueModified: root.outerGapRightModified(value)
                Accessible.name: i18n("Right gap")

                Binding on value {
                    value: root.outerGapRightValue
                    when: !rightGapSpinBox.activeFocus
                    restoreMode: Binding.RestoreNone
                }

            }

            Label {
                text: i18nc("@label", "px")
            }

        }

        CheckBox {
            Kirigami.FormData.label: i18n("Smart gaps:")
            text: i18n("Hide gaps when only one window is tiled")
            checked: root.smartGapsValue
            onToggled: root.smartGapsToggled(checked)
            Accessible.name: i18n("Smart gaps")
        }

    }

}
