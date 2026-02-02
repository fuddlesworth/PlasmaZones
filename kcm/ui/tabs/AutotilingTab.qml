// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Autotiling tab - Settings for automatic window tiling
 *
 * Provides configuration for autotiling algorithms, gaps, master area,
 * and behavior settings. Designed for Bismuth/Krohnkite refugees.
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    // Whether this tab is currently visible (for conditional tooltips)
    property bool isCurrentTab: false

    // Signal for border color dialog (handled by main.qml)
    signal requestActiveBorderColorDialog()

    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ═══════════════════════════════════════════════════════════════════════
        // ENABLE CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: enableCard.implicitHeight

            Kirigami.Card {
                id: enableCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Autotiling")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        id: autotileEnabledCheck
                        Kirigami.FormData.label: i18n("Enable:")
                        text: i18n("Automatically arrange windows using tiling algorithms")
                        checked: kcm.autotileEnabled
                        onToggled: kcm.autotileEnabled = checked
                    }

                    Label {
                        Kirigami.FormData.label: " "
                        text: i18n("When enabled, windows are automatically tiled using the selected algorithm. Manual zone layouts are suspended.")
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // ALGORITHM CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: algorithmCard.implicitHeight
            visible: autotileEnabledCheck.checked

            Kirigami.Card {
                id: algorithmCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Algorithm")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    ComboBox {
                        id: algorithmCombo
                        Kirigami.FormData.label: i18n("Tiling algorithm:")
                        textRole: "name"
                        valueRole: "id"
                        model: kcm.availableAlgorithms()
                        currentIndex: indexForValue(kcm.autotileAlgorithm)
                        onActivated: kcm.autotileAlgorithm = currentValue

                        function indexForValue(value) {
                            for (let i = 0; i < model.length; i++) {
                                if (model[i].id === value) return i
                            }
                            return 0
                        }
                    }

                    Label {
                        Kirigami.FormData.label: " "
                        text: {
                            const model = algorithmCombo.model
                            const idx = algorithmCombo.currentIndex
                            if (model && idx >= 0 && idx < model.length) {
                                return model[idx].description || ""
                            }
                            return ""
                        }
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        Layout.fillWidth: true
                    }

                    // Algorithm preview
                    Rectangle {
                        Kirigami.FormData.label: i18n("Preview:")
                        Layout.preferredWidth: 280
                        Layout.preferredHeight: 160
                        color: Kirigami.Theme.backgroundColor
                        border.color: Kirigami.Theme.disabledTextColor
                        border.width: 1
                        radius: 4

                        AlgorithmPreview {
                            anchors.fill: parent
                            anchors.margins: 8
                            algorithmId: kcm.autotileAlgorithm
                            windowCount: previewWindowSlider.value
                            splitRatio: kcm.autotileSplitRatio
                            masterCount: kcm.autotileMasterCount
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Preview windows:")
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: previewWindowSlider
                            Layout.preferredWidth: root.constants.sliderPreferredWidth
                            from: 1
                            to: 6
                            stepSize: 1
                            value: 4
                        }

                        Label {
                            text: Math.round(previewWindowSlider.value)
                            Layout.preferredWidth: root.constants.sliderValueLabelWidth
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // MASTER AREA CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: masterCard.implicitHeight
            visible: autotileEnabledCheck.checked

            Kirigami.Card {
                id: masterCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Master Area")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    RowLayout {
                        Kirigami.FormData.label: i18n("Master ratio:")
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: splitRatioSlider
                            Layout.preferredWidth: root.constants.sliderPreferredWidth
                            from: 0.1
                            to: 0.9
                            stepSize: 0.05
                            value: kcm.autotileSplitRatio
                            onMoved: kcm.autotileSplitRatio = value
                        }

                        Label {
                            text: Math.round(splitRatioSlider.value * 100) + "%"
                            Layout.preferredWidth: root.constants.sliderValueLabelWidth
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Master count:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 1
                            to: 5
                            value: kcm.autotileMasterCount
                            onValueModified: kcm.autotileMasterCount = value

                            ToolTip.visible: hovered && root.isCurrentTab
                            ToolTip.text: i18n("Number of windows in the master area")
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // GAPS CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: gapsCard.implicitHeight
            visible: autotileEnabledCheck.checked

            Kirigami.Card {
                id: gapsCard
                anchors.fill: parent

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
                            to: 50
                            value: kcm.autotileInnerGap
                            onValueModified: kcm.autotileInnerGap = value

                            ToolTip.visible: hovered && root.isCurrentTab
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
                            to: 50
                            value: kcm.autotileOuterGap
                            onValueModified: kcm.autotileOuterGap = value

                            ToolTip.visible: hovered && root.isCurrentTab
                            ToolTip.text: i18n("Gap from screen edges")
                        }

                        Label {
                            text: i18n("px")
                        }
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Smart gaps:")
                        text: i18n("Hide gaps when only one window is tiled")
                        checked: kcm.autotileSmartGaps
                        onToggled: kcm.autotileSmartGaps = checked
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // BEHAVIOR CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: behaviorCard.implicitHeight
            visible: autotileEnabledCheck.checked

            Kirigami.Card {
                id: behaviorCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Behavior")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    ComboBox {
                        id: insertPositionCombo
                        Kirigami.FormData.label: i18n("New windows:")
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: i18n("Add to end of stack"), value: 0 },
                            { text: i18n("Insert after focused"), value: 1 },
                            { text: i18n("Add as master"), value: 2 }
                        ]
                        currentIndex: indexForValue(kcm.autotileInsertPosition)
                        onActivated: kcm.autotileInsertPosition = currentValue

                        function indexForValue(value) {
                            for (let i = 0; i < model.length; i++) {
                                if (model[i].value === value) return i
                            }
                            return 0
                        }
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

                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: i18n("When enabled, moving mouse over a window focuses it")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Constraints:")
                        text: i18n("Respect window minimum size")
                        checked: kcm.autotileRespectMinimumSize
                        onToggled: kcm.autotileRespectMinimumSize = checked

                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: i18n("Windows won't be resized smaller than their minimum. May cause layout to not fill screen.")
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // VISUAL FEEDBACK CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: visualCard.implicitHeight
            visible: autotileEnabledCheck.checked

            Kirigami.Card {
                id: visualCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Visual Feedback")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        id: showActiveBorderCheck
                        Kirigami.FormData.label: i18n("Active border:")
                        text: i18n("Show border around focused window")
                        checked: kcm.autotileShowActiveBorder
                        onToggled: kcm.autotileShowActiveBorder = checked
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Border width:")
                        enabled: showActiveBorderCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 1
                            to: 10
                            value: kcm.autotileActiveBorderWidth
                            onValueModified: kcm.autotileActiveBorderWidth = value
                        }

                        Label {
                            text: i18n("px")
                        }
                    }

                    CheckBox {
                        id: useSystemBorderColorCheck
                        Kirigami.FormData.label: i18n("Border color:")
                        text: i18n("Use system highlight color")
                        checked: kcm.autotileUseSystemBorderColor
                        onToggled: kcm.autotileUseSystemBorderColor = checked
                        enabled: showActiveBorderCheck.checked
                    }

                    RowLayout {
                        Kirigami.FormData.label: " "
                        visible: !useSystemBorderColorCheck.checked && showActiveBorderCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        ColorButton {
                            color: kcm.autotileActiveBorderColor
                            onClicked: root.requestActiveBorderColorDialog()
                        }

                        Label {
                            text: kcm.autotileActiveBorderColor.toString().toUpperCase()
                            font: Kirigami.Theme.fixedWidthFont
                        }
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Animations")
                    }

                    CheckBox {
                        id: animationsEnabledCheck
                        Kirigami.FormData.label: i18n("Animations:")
                        text: i18n("Enable smooth tiling animations")
                        checked: kcm.autotileAnimationsEnabled
                        onToggled: kcm.autotileAnimationsEnabled = checked
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Duration:")
                        enabled: animationsEnabledCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: animationDurationSlider
                            Layout.preferredWidth: root.constants.sliderPreferredWidth
                            from: 50
                            to: 500
                            stepSize: 10
                            value: kcm.autotileAnimationDuration
                            onMoved: kcm.autotileAnimationDuration = Math.round(value)
                        }

                        Label {
                            text: Math.round(animationDurationSlider.value) + " ms"
                            Layout.preferredWidth: root.constants.sliderValueLabelWidth + 15
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // MONOCLE MODE CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: monocleCard.implicitHeight
            visible: autotileEnabledCheck.checked && kcm.autotileAlgorithm === "monocle"

            Kirigami.Card {
                id: monocleCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Monocle Mode")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        Kirigami.FormData.label: i18n("Other windows:")
                        text: i18n("Minimize non-focused windows")
                        checked: kcm.autotileMonocleHideOthers
                        onToggled: kcm.autotileMonocleHideOthers = checked

                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: i18n("When enabled, windows not in focus are minimized. Otherwise they remain behind the focused window.")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Tab bar:")
                        text: i18n("Show tab bar for window switching")
                        checked: kcm.autotileMonocleShowTabs
                        onToggled: kcm.autotileMonocleShowTabs = checked
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // SHORTCUTS CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: shortcutsCard.implicitHeight
            visible: autotileEnabledCheck.checked

            Kirigami.Card {
                id: shortcutsCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Shortcuts")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    KeySequenceInput {
                        Kirigami.FormData.label: i18n("Toggle autotiling:")
                        keySequence: kcm.autotileToggleShortcut
                        onKeySequenceChanged: kcm.autotileToggleShortcut = keySequence
                    }

                    KeySequenceInput {
                        Kirigami.FormData.label: i18n("Focus master:")
                        keySequence: kcm.autotileFocusMasterShortcut
                        onKeySequenceChanged: kcm.autotileFocusMasterShortcut = keySequence
                    }

                    KeySequenceInput {
                        Kirigami.FormData.label: i18n("Swap with master:")
                        keySequence: kcm.autotileSwapMasterShortcut
                        onKeySequenceChanged: kcm.autotileSwapMasterShortcut = keySequence
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Master Ratio")
                    }

                    KeySequenceInput {
                        Kirigami.FormData.label: i18n("Increase ratio:")
                        keySequence: kcm.autotileIncMasterRatioShortcut
                        onKeySequenceChanged: kcm.autotileIncMasterRatioShortcut = keySequence
                    }

                    KeySequenceInput {
                        Kirigami.FormData.label: i18n("Decrease ratio:")
                        keySequence: kcm.autotileDecMasterRatioShortcut
                        onKeySequenceChanged: kcm.autotileDecMasterRatioShortcut = keySequence
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Master Count")
                    }

                    KeySequenceInput {
                        Kirigami.FormData.label: i18n("Increase count:")
                        keySequence: kcm.autotileIncMasterCountShortcut
                        onKeySequenceChanged: kcm.autotileIncMasterCountShortcut = keySequence
                    }

                    KeySequenceInput {
                        Kirigami.FormData.label: i18n("Decrease count:")
                        keySequence: kcm.autotileDecMasterCountShortcut
                        onKeySequenceChanged: kcm.autotileDecMasterCountShortcut = keySequence
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                    }

                    KeySequenceInput {
                        Kirigami.FormData.label: i18n("Force retile:")
                        keySequence: kcm.autotileRetileShortcut
                        onKeySequenceChanged: kcm.autotileRetileShortcut = keySequence
                    }

                    Button {
                        Kirigami.FormData.label: " "
                        text: i18n("Reset Shortcuts to Defaults")
                        icon.name: "edit-undo"
                        onClicked: kcm.resetAutotileShortcuts()
                    }
                }
            }
        }
    }
}
