// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

/**
 * @brief Autotiling sub-KCM -- Tiling algorithms, gaps, and behavior settings
 *
 * Standalone version of the former AutotilingTab from the monolith KCM.
 * Color dialogs are handled inline.
 */
KCMUtils.SimpleKCM {
    // ═══════════════════════════════════════════════════════════════════════
    // DIALOGS
    // ═══════════════════════════════════════════════════════════════════════

    id: root

    // Capture the context property so child components can access it via root.kcmModule
    readonly property var kcmModule: kcm
    // Layout constants (previously from monolith's QtObject)
    readonly property int autotileGapMax: 50
    readonly property int algorithmPreviewWidth: 280
    readonly property int algorithmPreviewHeight: 160
    readonly property int sliderValueLabelWidth: 40
    // Per-screen override helper (applies to Algorithm + Gaps cards)
    property alias selectedScreenName: psHelper.selectedScreenName
    readonly property alias isPerScreen: psHelper.isPerScreen
    readonly property alias hasOverrides: psHelper.hasOverrides
    // Convenience property for algorithm-specific visibility
    readonly property string effectiveAlgorithm: settingValue("Algorithm", kcm.autotileAlgorithm)

    function settingValue(key, globalValue) {
        return psHelper.settingValue(key, globalValue);
    }

    function writeSetting(key, value, globalSetter) {
        psHelper.writeSetting(key, value, globalSetter);
    }

    PerScreenOverrideHelper {
        id: psHelper

        kcm: root.kcmModule
        getterMethod: "getPerScreenAutotileSettings"
        setterMethod: "setPerScreenAutotileSetting"
        clearerMethod: "clearPerScreenAutotileSettings"
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // Enable toggle - prominent at top
        CheckBox {
            id: autotileEnabledCheck

            Layout.fillWidth: true
            text: i18n("Enable automatic window tiling")
            checked: kcm.autotileEnabled
            onToggled: kcm.autotileEnabled = checked
            font.bold: true
            Accessible.name: i18n("Enable autotiling")
        }

        // ═══════════════════════════════════════════════════════════════════════
        // BEHAVIOR CARD (global)
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: behaviorCard.implicitHeight

            Kirigami.Card {
                id: behaviorCard

                anchors.fill: parent
                enabled: kcm.autotileEnabled

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Behavior")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    WideComboBox {
                        id: insertPositionCombo

                        Kirigami.FormData.label: i18n("New windows:")
                        textRole: "text"
                        valueRole: "value"
                        model: [{
                            "text": i18n("Add after existing windows"),
                            "value": 0
                        }, {
                            "text": i18n("Insert after focused"),
                            "value": 1
                        }, {
                            "text": i18n("Add as main window"),
                            "value": 2
                        }]
                        currentIndex: Math.max(0, indexOfValue(kcm.autotileInsertPosition))
                        onActivated: kcm.autotileInsertPosition = currentValue
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Focus:")
                        text: i18n("Automatically focus newly opened windows")
                        checked: kcm.autotileFocusNewWindows
                        onToggled: kcm.autotileFocusNewWindows = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: " "
                        text: i18n("Focus follows mouse pointer")
                        checked: kcm.autotileFocusFollowsMouse
                        onToggled: kcm.autotileFocusFollowsMouse = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, moving mouse over a window focuses it")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Constraints:")
                        text: i18n("Respect window minimum size")
                        checked: kcm.autotileRespectMinimumSize
                        onToggled: kcm.autotileRespectMinimumSize = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Windows will not be resized below their minimum size. This may leave gaps in the layout.")
                    }

                    CheckBox {
                        id: hideTitleBarsCheck

                        Kirigami.FormData.label: i18n("Decorations:")
                        text: i18n("Hide title bars on tiled windows")
                        checked: kcm.autotileHideTitleBars
                        onToggled: kcm.autotileHideTitleBars = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Remove window title bars while autotiled. Restored when floating or leaving autotile mode.")
                    }

                    // ── Border settings (visible when title bars hidden) ──
                    RowLayout {
                        Kirigami.FormData.label: i18n("Border width:")
                        visible: hideTitleBarsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: 10
                            value: kcm.autotileBorderWidth
                            onValueModified: kcm.autotileBorderWidth = value
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Colored border drawn around borderless tiled windows (0 to disable)")
                        }

                        Label {
                            text: i18n("px")
                        }

                    }

                    CheckBox {
                        id: useSystemBorderColorsCheck

                        Kirigami.FormData.label: i18n("Color scheme:")
                        visible: hideTitleBarsCheck.checked
                        text: i18n("Use system accent color")
                        checked: kcm.autotileUseSystemBorderColors
                        onToggled: kcm.autotileUseSystemBorderColors = checked
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Border:")
                        visible: hideTitleBarsCheck.checked && !useSystemBorderColorsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        ColorButton {
                            color: kcm.autotileBorderColor
                            onClicked: {
                                autotileBorderColorDialog.selectedColor = kcm.autotileBorderColor;
                                autotileBorderColorDialog.open();
                            }
                        }

                        Label {
                            text: kcm.autotileBorderColor.toString().toUpperCase()
                            font: Kirigami.Theme.fixedWidthFont
                        }

                    }

                }

            }

        }

        MonitorSelectorSection {
            Layout.fillWidth: true
            kcm: root.kcmModule
            featureEnabled: kcm.autotileEnabled
            selectedScreenName: root.selectedScreenName
            hasOverrides: root.hasOverrides
            onSelectedScreenNameChanged: root.selectedScreenName = selectedScreenName
            onResetClicked: psHelper.clearOverrides()
        }

        // ═══════════════════════════════════════════════════════════════════════
        // GAPS CARD (per-monitor)
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: gapsCard.implicitHeight

            Kirigami.Card {
                id: gapsCard

                anchors.fill: parent
                enabled: kcm.autotileEnabled

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Gaps")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    RowLayout {
                        Kirigami.FormData.label: i18n("Inner gap:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: root.autotileGapMax
                            value: root.settingValue("InnerGap", kcm.autotileInnerGap)
                            onValueModified: root.writeSetting("InnerGap", value, function(v) {
                                kcm.autotileInnerGap = v;
                            })
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Gap between tiled windows")
                        }

                        Label {
                            text: i18n("px")
                        }

                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Outer gap:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: root.autotileGapMax
                            value: root.settingValue("OuterGap", kcm.autotileOuterGap)
                            enabled: !autotilePerSideCheck.checked
                            onValueModified: root.writeSetting("OuterGap", value, function(v) {
                                kcm.autotileOuterGap = v;
                            })
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Gap from screen edges")
                        }

                        Label {
                            text: i18n("px")
                            visible: !autotilePerSideCheck.checked
                        }

                        CheckBox {
                            id: autotilePerSideCheck

                            text: i18n("Set per side")
                            checked: root.settingValue("UsePerSideOuterGap", kcm.autotileUsePerSideOuterGap)
                            onToggled: root.writeSetting("UsePerSideOuterGap", checked, function(v) {
                                kcm.autotileUsePerSideOuterGap = v;
                            })
                        }

                    }

                    GridLayout {
                        Kirigami.FormData.label: i18n("Per-side gaps:")
                        visible: autotilePerSideCheck.checked
                        columns: 6
                        columnSpacing: Kirigami.Units.smallSpacing
                        rowSpacing: Kirigami.Units.smallSpacing

                        Label {
                            text: i18n("Top:")
                        }

                        SpinBox {
                            from: 0
                            to: root.autotileGapMax
                            value: root.settingValue("OuterGapTop", kcm.autotileOuterGapTop)
                            onValueModified: root.writeSetting("OuterGapTop", value, function(v) {
                                kcm.autotileOuterGapTop = v;
                            })
                            Accessible.name: i18nc("@label", "Top edge gap")
                        }

                        Label {
                            text: i18nc("@label", "px")
                        }

                        Label {
                            text: i18n("Bottom:")
                        }

                        SpinBox {
                            from: 0
                            to: root.autotileGapMax
                            value: root.settingValue("OuterGapBottom", kcm.autotileOuterGapBottom)
                            onValueModified: root.writeSetting("OuterGapBottom", value, function(v) {
                                kcm.autotileOuterGapBottom = v;
                            })
                            Accessible.name: i18nc("@label", "Bottom edge gap")
                        }

                        Label {
                            text: i18nc("@label", "px")
                        }

                        Label {
                            text: i18n("Left:")
                        }

                        SpinBox {
                            from: 0
                            to: root.autotileGapMax
                            value: root.settingValue("OuterGapLeft", kcm.autotileOuterGapLeft)
                            onValueModified: root.writeSetting("OuterGapLeft", value, function(v) {
                                kcm.autotileOuterGapLeft = v;
                            })
                            Accessible.name: i18nc("@label", "Left edge gap")
                        }

                        Label {
                            text: i18nc("@label", "px")
                        }

                        Label {
                            text: i18n("Right:")
                        }

                        SpinBox {
                            from: 0
                            to: root.autotileGapMax
                            value: root.settingValue("OuterGapRight", kcm.autotileOuterGapRight)
                            onValueModified: root.writeSetting("OuterGapRight", value, function(v) {
                                kcm.autotileOuterGapRight = v;
                            })
                            Accessible.name: i18nc("@label", "Right edge gap")
                        }

                        Label {
                            text: i18nc("@label", "px")
                        }

                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Smart gaps:")
                        text: i18n("Hide gaps when only one window is tiled")
                        checked: root.settingValue("SmartGaps", kcm.autotileSmartGaps)
                        onToggled: root.writeSetting("SmartGaps", checked, function(v) {
                            kcm.autotileSmartGaps = v;
                        })
                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════════════
        // ALGORITHM CARD (per-monitor, extracted component)
        // ═══════════════════════════════════════════════════════════════════════
        AlgorithmCard {
            Layout.fillWidth: true
            kcm: root.kcmModule
            constants: root
        }

    }

    ColorDialog {
        id: autotileBorderColorDialog

        title: i18n("Choose Border Color")
        onAccepted: kcm.autotileBorderColor = selectedColor
    }

}
