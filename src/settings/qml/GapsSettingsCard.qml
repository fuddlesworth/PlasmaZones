// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Shared Gaps card for both Tiling → Appearance and Snapping → Window
// Appearance. The two modes differ only in labels and whether Smart gaps is
// shown, so they are exposed as properties; the per-screen wiring lives in the
// host page, which feeds the *Value properties and handles the *Modified
// signals.
SettingsCard {
    id: root

    required property int gapMax
    required property int gapMin
    // The "primary" gap is the inter-window spacing ("Inner gap" — both tiling
    // and snapping share the label via primaryGapLabel). It has its own bounds
    // (clamped against a different ConfigDefaults range than the outer/per-side
    // gaps), so the UI range always matches the validator's clamp range.
    required property int primaryGapMin
    required property int primaryGapMax
    required property int primaryGapValue
    required property int outerGapValue
    required property bool usePerSideOuterGap
    required property int outerGapTopValue
    required property int outerGapBottomValue
    required property int outerGapLeftValue
    required property int outerGapRightValue
    // Smart gaps is autotile-only; snapping hides the row.
    property bool showSmartGaps: true
    property bool smartGapsValue: false
    // Defaults; hosts may override the label/description per mode. Tiling and
    // snapping currently share "Inner gap" / "Outer gap" for cross-mode
    // consistency (snapping just overrides the descriptions).
    property string primaryGapLabel: i18n("Inner gap")
    property string primaryGapDescription: i18n("Space between tiled windows")
    property string outerGapLabel: i18n("Outer gap")
    property string outerGapDescription: i18n("Space from screen edges to tiled windows")

    signal primaryGapModified(int value)
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
            title: root.primaryGapLabel
            searchAnchor: "primaryGap"
            description: root.primaryGapDescription

            SpinBox {
                id: primaryGapSpinBox

                from: root.primaryGapMin
                to: root.primaryGapMax
                onValueModified: root.primaryGapModified(value)
                Accessible.name: root.primaryGapLabel

                Binding on value {
                    value: root.primaryGapValue
                    when: !primaryGapSpinBox.activeFocus
                    restoreMode: Binding.RestoreNone
                }
            }
        }

        SettingsRow {
            visible: !tilePerSideSwitch.checked
            title: root.outerGapLabel
            searchAnchor: "outerGap"
            description: root.outerGapDescription

            SpinBox {
                id: outerGapSpinBox

                from: root.gapMin
                to: root.gapMax
                onValueModified: root.outerGapModified(value)
                Accessible.name: root.outerGapLabel

                Binding on value {
                    value: root.outerGapValue
                    when: !outerGapSpinBox.activeFocus
                    restoreMode: Binding.RestoreNone
                }
            }
        }

        // Per-side gaps and Smart gaps are fine-tuning — advanced only.
        AdvancedGroup {
            SettingsSeparator {}

            SettingsRow {
                title: i18n("Per-side outer gaps")
                searchAnchor: "perSideOuterGaps"
                description: tilePerSideSwitch.checked ? i18n("Set different gap sizes for each screen edge") : i18n("Use a single outer gap value for all edges")

                SettingsSwitch {
                    id: tilePerSideSwitch

                    checked: root.usePerSideOuterGap
                    accessibleName: i18n("Set gaps per side")
                    onToggled: function (newValue) {
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
                visible: root.showSmartGaps
            }

            SettingsRow {
                visible: root.showSmartGaps
                title: i18n("Smart gaps")
                searchAnchor: "smartGaps"
                description: i18n("Remove all gaps when only one window is tiled")

                SettingsSwitch {
                    checked: root.smartGapsValue
                    accessibleName: i18n("Smart gaps")
                    onToggled: function (newValue) {
                        root.smartGapsToggled(newValue);
                    }
                }
            }
        }
    }
}
