// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    // Layout constants
    readonly property int gapMax: 50
    readonly property int sliderValueLabelWidth: 40
    readonly property bool bordersActive: hideTitleBarsCheck.checked || showBorderCheck.checked
    // Algorithm list (14 algorithms matching the KCM)
    readonly property var algorithmModel: [{
        "id": "master-stack",
        "name": i18n("Master-Stack")
    }, {
        "id": "dwindle",
        "name": i18n("Dwindle")
    }, {
        "id": "spiral",
        "name": i18n("Spiral")
    }, {
        "id": "grid",
        "name": i18n("Grid")
    }, {
        "id": "columns",
        "name": i18n("Columns")
    }, {
        "id": "rows",
        "name": i18n("Rows")
    }, {
        "id": "bsp",
        "name": i18n("BSP")
    }, {
        "id": "centered-master",
        "name": i18n("Centered Master")
    }, {
        "id": "monocle",
        "name": i18n("Monocle")
    }, {
        "id": "wide",
        "name": i18n("Wide")
    }, {
        "id": "three-column",
        "name": i18n("Three Column")
    }, {
        "id": "cascade",
        "name": i18n("Cascade")
    }, {
        "id": "spread",
        "name": i18n("Spread")
    }, {
        "id": "stair",
        "name": i18n("Stair")
    }]

    function algorithmIndexOf(algoId) {
        for (var i = 0; i < algorithmModel.length; i++) {
            if (algorithmModel[i].id === algoId)
                return i;

        }
        return 0;
    }

    contentHeight: content.implicitHeight

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =====================================================================
        // Enable toggle
        // =====================================================================
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing

            Label {
                text: i18n("Enable Automatic Tiling")
                font.bold: true
            }

            Item {
                Layout.fillWidth: true
            }

            Switch {
                checked: kcm.autotileEnabled
                onToggled: kcm.autotileEnabled = checked
                Accessible.name: i18n("Enable automatic tiling")
            }

        }

        // =====================================================================
        // Algorithm Card
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: kcm.autotileEnabled

            header: Kirigami.Heading {
                text: i18n("Algorithm")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Algorithm selector
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Algorithm:")
                    }

                    ComboBox {
                        id: algoCombo

                        Layout.fillWidth: true
                        textRole: "name"
                        valueRole: "id"
                        model: root.algorithmModel
                        Component.onCompleted: currentIndex = root.algorithmIndexOf(kcm.autotileAlgorithm)
                        onActivated: kcm.autotileAlgorithm = currentValue
                        Accessible.name: i18n("Tiling algorithm")

                        Connections {
                            function onAutotileAlgorithmChanged() {
                                algoCombo.currentIndex = root.algorithmIndexOf(kcm.autotileAlgorithm);
                            }

                            target: kcm
                        }

                    }

                }

                // Split ratio slider (general -- hidden for centered-master)
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: algoCombo.currentValue !== "centered-master"

                    Label {
                        text: i18n("Split ratio:")
                    }

                    Slider {
                        id: splitRatioSlider

                        Layout.fillWidth: true
                        from: 0.1
                        to: 0.9
                        stepSize: 0.05
                        value: kcm.autotileSplitRatio
                        onMoved: kcm.autotileSplitRatio = value
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Proportion of screen width for the master area")
                    }

                    Label {
                        text: Math.round(splitRatioSlider.value * 100) + "%"
                        Layout.preferredWidth: root.sliderValueLabelWidth
                        horizontalAlignment: Text.AlignRight
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                // Master count (general -- hidden for centered-master)
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: algoCombo.currentValue !== "centered-master"

                    Label {
                        text: i18n("Master count:")
                    }

                    SpinBox {
                        from: 1
                        to: 5
                        value: kcm.autotileMasterCount
                        onValueModified: kcm.autotileMasterCount = value
                        Accessible.name: i18n("Master count")
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Number of windows in the master area")
                    }

                }

                // Centered master specific settings
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: algoCombo.currentValue === "centered-master"

                    Label {
                        text: i18n("Center ratio:")
                    }

                    Slider {
                        id: centeredSplitRatioSlider

                        Layout.fillWidth: true
                        from: 0.1
                        to: 0.9
                        stepSize: 0.05
                        value: kcm.autotileCenteredMasterSplitRatio
                        onMoved: kcm.autotileCenteredMasterSplitRatio = value
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Proportion of screen width for the center column")
                    }

                    Label {
                        text: Math.round(centeredSplitRatioSlider.value * 100) + "%"
                        Layout.preferredWidth: root.sliderValueLabelWidth
                        horizontalAlignment: Text.AlignRight
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: algoCombo.currentValue === "centered-master"

                    Label {
                        text: i18n("Center count:")
                    }

                    SpinBox {
                        from: 1
                        to: 5
                        value: kcm.autotileCenteredMasterMasterCount
                        onValueModified: kcm.autotileCenteredMasterMasterCount = value
                        Accessible.name: i18n("Center count")
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Number of windows in the center area")
                    }

                }

                // Max windows
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Max windows:")
                    }

                    SpinBox {
                        from: 1
                        to: 12
                        value: kcm.autotileMaxWindows
                        onValueModified: kcm.autotileMaxWindows = value
                        Accessible.name: i18n("Maximum tiled windows")
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Maximum number of windows to tile with this algorithm")
                    }

                }

            }

        }

        // =====================================================================
        // Gaps Card
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: kcm.autotileEnabled

            header: Kirigami.Heading {
                text: i18n("Gaps")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Inner gap
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Inner gap:")
                    }

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: kcm.autotileInnerGap
                        onValueModified: kcm.autotileInnerGap = value
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Gap between tiled windows")
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                // Outer gap
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Outer gap:")
                    }

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: kcm.autotileOuterGap
                        enabled: !perSideCheck.checked
                        onValueModified: kcm.autotileOuterGap = value
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Gap from screen edges")
                    }

                    Label {
                        text: i18n("px")
                        visible: !perSideCheck.checked
                    }

                    CheckBox {
                        id: perSideCheck

                        text: i18n("Set per side")
                        checked: kcm.autotileUsePerSideOuterGap
                        onToggled: kcm.autotileUsePerSideOuterGap = checked
                    }

                }

                // Per-side gap spinboxes
                GridLayout {
                    columns: 6
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing
                    visible: perSideCheck.checked
                    Layout.leftMargin: Kirigami.Units.largeSpacing

                    Label {
                        text: i18n("Top:")
                    }

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: kcm.autotileOuterGapTop
                        onValueModified: kcm.autotileOuterGapTop = value
                        Accessible.name: i18n("Top edge gap")
                    }

                    Label {
                        text: i18n("px")
                    }

                    Label {
                        text: i18n("Bottom:")
                    }

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: kcm.autotileOuterGapBottom
                        onValueModified: kcm.autotileOuterGapBottom = value
                        Accessible.name: i18n("Bottom edge gap")
                    }

                    Label {
                        text: i18n("px")
                    }

                    Label {
                        text: i18n("Left:")
                    }

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: kcm.autotileOuterGapLeft
                        onValueModified: kcm.autotileOuterGapLeft = value
                        Accessible.name: i18n("Left edge gap")
                    }

                    Label {
                        text: i18n("px")
                    }

                    Label {
                        text: i18n("Right:")
                    }

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: kcm.autotileOuterGapRight
                        onValueModified: kcm.autotileOuterGapRight = value
                        Accessible.name: i18n("Right edge gap")
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                // Smart gaps
                CheckBox {
                    text: i18n("Hide gaps when only one window is tiled")
                    checked: kcm.autotileSmartGaps
                    onToggled: kcm.autotileSmartGaps = checked
                }

            }

        }

        // =====================================================================
        // Behavior Card
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: kcm.autotileEnabled

            header: Kirigami.Heading {
                text: i18n("Behavior")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Insert position
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("New windows:")
                    }

                    ComboBox {
                        Layout.fillWidth: true
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

                }

                // Focus settings
                CheckBox {
                    text: i18n("Automatically focus newly opened windows")
                    checked: kcm.autotileFocusNewWindows
                    onToggled: kcm.autotileFocusNewWindows = checked
                }

                CheckBox {
                    text: i18n("Focus follows mouse pointer")
                    checked: kcm.autotileFocusFollowsMouse
                    onToggled: kcm.autotileFocusFollowsMouse = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("When enabled, moving mouse over a window focuses it")
                }

                // Constraints
                CheckBox {
                    text: i18n("Respect window minimum size")
                    checked: kcm.autotileRespectMinimumSize
                    onToggled: kcm.autotileRespectMinimumSize = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Windows will not be resized below their minimum size. This may leave gaps in the layout.")
                }

            }

        }

        // =====================================================================
        // Borders Card
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: kcm.autotileEnabled

            header: Kirigami.Heading {
                text: i18n("Borders")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Hide title bars
                CheckBox {
                    id: hideTitleBarsCheck

                    text: i18n("Hide title bars on tiled windows")
                    checked: kcm.autotileHideTitleBars
                    onToggled: kcm.autotileHideTitleBars = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Remove window title bars while autotiled. Restored when floating or leaving autotile mode.")
                }

                // Show border
                CheckBox {
                    id: showBorderCheck

                    text: i18n("Show borders in tiling mode")
                    checked: kcm.autotileShowBorder
                    onToggled: kcm.autotileShowBorder = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Draw colored borders around all windows in tiling mode. Active color for focused, inactive for unfocused.")
                }

                // Border width
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: root.bordersActive

                    Label {
                        text: i18n("Width:")
                    }

                    SpinBox {
                        from: 0
                        to: 10
                        value: kcm.autotileBorderWidth
                        onValueModified: kcm.autotileBorderWidth = value
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Colored border drawn around tiled windows (0 to disable)")
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                // Border radius
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: root.bordersActive

                    Label {
                        text: i18n("Corner radius:")
                    }

                    SpinBox {
                        from: 0
                        to: 20
                        value: kcm.autotileBorderRadius
                        onValueModified: kcm.autotileBorderRadius = value
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Corner radius for the border (0 for square corners)")
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                // Use system colors
                CheckBox {
                    id: useSystemColorsCheck

                    text: i18n("Use system accent color")
                    checked: kcm.autotileUseSystemBorderColors
                    onToggled: kcm.autotileUseSystemBorderColors = checked
                }

                // Active border color
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: !useSystemColorsCheck.checked

                    Label {
                        text: i18n("Active color:")
                    }

                    Rectangle {
                        width: 32
                        height: 32
                        radius: Kirigami.Units.smallSpacing
                        color: kcm.autotileBorderColor
                        border.color: Kirigami.Theme.disabledTextColor
                        border.width: 1
                        Accessible.name: i18n("Active border color picker")
                        Accessible.role: Accessible.Button

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                activeBorderColorDialog.selectedColor = kcm.autotileBorderColor;
                                activeBorderColorDialog.open();
                            }
                        }

                    }

                    Label {
                        text: kcm.autotileBorderColor.toString().toUpperCase()
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                // Inactive border color
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: !useSystemColorsCheck.checked

                    Label {
                        text: i18n("Inactive color:")
                    }

                    Rectangle {
                        width: 32
                        height: 32
                        radius: Kirigami.Units.smallSpacing
                        color: kcm.autotileInactiveBorderColor
                        border.color: Kirigami.Theme.disabledTextColor
                        border.width: 1
                        Accessible.name: i18n("Inactive border color picker")
                        Accessible.role: Accessible.Button

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                inactiveBorderColorDialog.selectedColor = kcm.autotileInactiveBorderColor;
                                inactiveBorderColorDialog.open();
                            }
                        }

                    }

                    Label {
                        text: kcm.autotileInactiveBorderColor.toString().toUpperCase()
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

            }

        }

    }

    // =========================================================================
    // Color Dialogs
    // =========================================================================
    ColorDialog {
        id: activeBorderColorDialog

        title: i18n("Choose Active Border Color")
        onAccepted: kcm.autotileBorderColor = selectedColor
    }

    ColorDialog {
        id: inactiveBorderColorDialog

        title: i18n("Choose Inactive Border Color")
        onAccepted: kcm.autotileInactiveBorderColor = selectedColor
    }

}
