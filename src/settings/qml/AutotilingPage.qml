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
                level: 3
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: Kirigami.FormLayout {
                ComboBox {
                    id: algoCombo

                    Kirigami.FormData.label: i18n("Algorithm:")
                    Layout.fillWidth: true
                    textRole: "name"
                    valueRole: "id"
                    model: root.algorithmModel
                    Component.onCompleted: currentIndex = root.algorithmIndexOf(kcm.autotileAlgorithm)
                    onActivated: kcm.autotileAlgorithm = currentValue
                    Accessible.name: i18n("Tiling algorithm")
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Select how windows are automatically arranged on screen")

                    Connections {
                        function onAutotileAlgorithmChanged() {
                            algoCombo.currentIndex = root.algorithmIndexOf(kcm.autotileAlgorithm);
                        }

                        target: kcm
                    }

                }

                // Algorithm preview — shows zone layout for selected algorithm
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 9 / 16 // 16:9 aspect
                    Layout.maximumHeight: Kirigami.Units.gridUnit * 12
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    Kirigami.FormData.label: " "

                    Rectangle {
                        // Find zones from layout list for current algorithm
                        property var zones: {
                            let algoId = "autotile:" + algoCombo.currentValue;
                            let layouts = settingsController.layouts;
                            for (let i = 0; i < layouts.length; i++) {
                                if (layouts[i].id === algoId)
                                    return layouts[i].zones || [];

                            }
                            return [];
                        }

                        anchors.fill: parent
                        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.5)
                        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
                        border.width: 1
                        radius: Kirigami.Units.smallSpacing

                        Repeater {
                            model: parent.zones

                            Rectangle {
                                required property var modelData
                                required property int index
                                readonly property var geo: modelData.relativeGeometry || modelData

                                x: (geo.x || 0) * parent.width + 2
                                y: (geo.y || 0) * parent.height + 2
                                width: Math.max(4, (geo.width || geo.w || 0) * parent.width - 4)
                                height: Math.max(4, (geo.height || geo.h || 0) * parent.height - 4)
                                color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.25)
                                border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
                                border.width: 1
                                radius: 2

                                Label {
                                    anchors.centerIn: parent
                                    text: (index + 1).toString()
                                    font.pixelSize: Math.max(8, Math.min(parent.width, parent.height) * 0.35)
                                    font.bold: true
                                    opacity: 0.6
                                    visible: parent.width > 16 && parent.height > 16
                                }

                            }

                        }

                        Label {
                            anchors.centerIn: parent
                            text: i18n("No preview available")
                            opacity: 0.4
                            visible: parent.zones.length === 0
                        }

                    }

                }

                // Algorithm description
                Label {
                    Kirigami.FormData.label: " "
                    Layout.fillWidth: true
                    text: {
                        const model = algoCombo.model;
                        const idx = algoCombo.currentIndex;
                        if (model && idx >= 0 && idx < model.length)
                            return model[idx].description || "";

                        return "";
                    }
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                }

                // Split ratio slider (general -- hidden for centered-master)
                RowLayout {
                    Kirigami.FormData.label: i18n("Split ratio:")
                    spacing: Kirigami.Units.smallSpacing
                    visible: algoCombo.currentValue !== "centered-master"

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
                    Kirigami.FormData.label: i18n("Master count:")
                    spacing: Kirigami.Units.smallSpacing
                    visible: algoCombo.currentValue !== "centered-master"

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
                    Kirigami.FormData.label: i18n("Center ratio:")
                    spacing: Kirigami.Units.smallSpacing
                    visible: algoCombo.currentValue === "centered-master"

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
                    Kirigami.FormData.label: i18n("Center count:")
                    spacing: Kirigami.Units.smallSpacing
                    visible: algoCombo.currentValue === "centered-master"

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
                    Kirigami.FormData.label: i18n("Max windows:")
                    spacing: Kirigami.Units.smallSpacing

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
                level: 3
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: Kirigami.FormLayout {
                RowLayout {
                    Kirigami.FormData.label: i18n("Inner gap:")
                    spacing: Kirigami.Units.smallSpacing

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

                RowLayout {
                    Kirigami.FormData.label: i18n("Outer gap:")
                    spacing: Kirigami.Units.smallSpacing

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

                GridLayout {
                    Kirigami.FormData.label: i18n("Per-side gaps:")
                    visible: perSideCheck.checked
                    columns: 6
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Top:")
                    }

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: kcm.autotileOuterGapTop
                        onValueModified: kcm.autotileOuterGapTop = value
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
                        to: root.gapMax
                        value: kcm.autotileOuterGapBottom
                        onValueModified: kcm.autotileOuterGapBottom = value
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
                        to: root.gapMax
                        value: kcm.autotileOuterGapLeft
                        onValueModified: kcm.autotileOuterGapLeft = value
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
                        to: root.gapMax
                        value: kcm.autotileOuterGapRight
                        onValueModified: kcm.autotileOuterGapRight = value
                        Accessible.name: i18nc("@label", "Right edge gap")
                    }

                    Label {
                        text: i18nc("@label", "px")
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

        // =====================================================================
        // Behavior Card
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: kcm.autotileEnabled

            header: Kirigami.Heading {
                text: i18n("Behavior")
                level: 3
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: Kirigami.FormLayout {
                ComboBox {
                    Kirigami.FormData.label: i18n("New windows:")
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

            }

        }

        // =====================================================================
        // Appearance Card (Borders + Colors, matching KCM structure)
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: kcm.autotileEnabled

            header: Kirigami.Heading {
                text: i18n("Appearance")
                level: 3
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: Kirigami.FormLayout {
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Colors")
                }

                CheckBox {
                    id: useSystemColorsCheck

                    Kirigami.FormData.label: i18n("Color scheme:")
                    text: i18n("Use system accent color")
                    checked: kcm.autotileUseSystemBorderColors
                    onToggled: kcm.autotileUseSystemBorderColors = checked
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Active color:")
                    visible: !useSystemColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

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

                RowLayout {
                    Kirigami.FormData.label: i18n("Inactive color:")
                    visible: !useSystemColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

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

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Decorations")
                }

                CheckBox {
                    id: hideTitleBarsCheck

                    Kirigami.FormData.label: i18n("Title bars:")
                    text: i18n("Hide title bars on tiled windows")
                    checked: kcm.autotileHideTitleBars
                    onToggled: kcm.autotileHideTitleBars = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Remove window title bars while autotiled. Restored when floating or leaving autotile mode.")
                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Borders")
                }

                CheckBox {
                    id: showBorderCheck

                    Kirigami.FormData.label: i18n("Border:")
                    text: i18n("Show borders in tiling mode")
                    checked: kcm.autotileShowBorder
                    onToggled: kcm.autotileShowBorder = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Draw colored borders around all windows in tiling mode. Active color for focused, inactive for unfocused. Works with or without hidden title bars.")
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Width:")
                    visible: root.bordersActive
                    spacing: Kirigami.Units.smallSpacing

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

                RowLayout {
                    Kirigami.FormData.label: i18n("Corner radius:")
                    visible: root.bordersActive
                    spacing: Kirigami.Units.smallSpacing

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
