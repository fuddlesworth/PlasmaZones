// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Display tab - Zone selector popup and OSD settings
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    // Whether this tab is currently visible (for conditional tooltips)
    property bool isCurrentTab: false

    // Screen aspect ratio for preview calculations (with safety check)
    property real screenAspectRatio: 16/9
    readonly property real safeAspectRatio: screenAspectRatio > 0 ? screenAspectRatio : (16/9)

    // Effective preview dimensions based on size mode
    property int effectivePreviewWidth: kcm.zoneSelectorSizeMode === 0
        ? Math.round(180 * (safeAspectRatio / (16/9)))
        : kcm.zoneSelectorPreviewWidth
    property int effectivePreviewHeight: kcm.zoneSelectorSizeMode === 0
        ? Math.round(effectivePreviewWidth / safeAspectRatio)
        : kcm.zoneSelectorPreviewHeight

    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // Info message
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("The zone selector popup appears when dragging windows to screen edges, allowing quick layout selection.")
            visible: true
        }

        // Enable toggle - prominent at top
        CheckBox {
            Layout.fillWidth: true
            text: i18n("Enable zone selector popup")
            checked: kcm.zoneSelectorEnabled
            onToggled: kcm.zoneSelectorEnabled = checked
            font.bold: true
        }

        // Position & Trigger card - wrapped in Item for stable sizing
        Item {
            Layout.fillWidth: true
            implicitHeight: positionCard.implicitHeight

            Kirigami.Card {
                id: positionCard
                anchors.fill: parent
                enabled: kcm.zoneSelectorEnabled

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Position & Trigger")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.largeSpacing

                    // Centered position picker with description
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: positionPicker.height + Kirigami.Units.gridUnit * 4

                        PositionPicker {
                            id: positionPicker
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            position: kcm.zoneSelectorPosition
                            enabled: kcm.zoneSelectorEnabled
                            onPositionSelected: function(newPosition) {
                                kcm.zoneSelectorPosition = newPosition
                            }
                        }

                        Label {
                            id: positionDescription
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: positionPicker.bottom
                            anchors.topMargin: Kirigami.Units.smallSpacing
                            text: i18n("Choose where the popup appears on screen")
                            opacity: 0.7
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                        }
                    }

                    // Trigger distance - centered like position picker
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: triggerColumn.implicitHeight + Kirigami.Units.gridUnit * 2

                        ColumnLayout {
                            id: triggerColumn
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            spacing: Kirigami.Units.smallSpacing
                            width: Math.min(Kirigami.Units.gridUnit * 25, parent.width)

                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: i18n("Trigger Distance")
                                font.bold: true
                            }

                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: i18n("How close to the screen edge before the popup appears")
                                opacity: 0.7
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Slider {
                                    id: triggerSlider
                                    Layout.fillWidth: true
                                    from: 10
                                    to: root.constants.zoneSelectorTriggerMax
                                    value: kcm.zoneSelectorTriggerDistance
                                    stepSize: 10
                                    onMoved: kcm.zoneSelectorTriggerDistance = value
                                }

                                Label {
                                    text: kcm.zoneSelectorTriggerDistance + " px"
                                    Layout.preferredWidth: root.constants.sliderValueLabelWidth + 15
                                    horizontalAlignment: Text.AlignRight
                                    font: Kirigami.Theme.fixedWidthFont
                                }
                            }
                        }
                    }
                }
            }
        }

        // Layout Arrangement card - wrapped in Item for stable sizing
        Item {
            Layout.fillWidth: true
            implicitHeight: layoutCard.implicitHeight

            Kirigami.Card {
                id: layoutCard
                anchors.fill: parent
                enabled: kcm.zoneSelectorEnabled

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Layout Arrangement")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    ComboBox {
                        id: zoneSelectorLayoutModeCombo
                        Kirigami.FormData.label: i18n("Arrangement:")
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: i18n("Grid"), value: 0 },
                            { text: i18n("Horizontal"), value: 1 },
                            { text: i18n("Vertical"), value: 2 }
                        ]
                        currentIndex: indexForValue(kcm.zoneSelectorLayoutMode)
                        onActivated: kcm.zoneSelectorLayoutMode = currentValue

                        function indexForValue(value) {
                            for (let i = 0; i < model.length; i++) {
                                if (model[i].value === value) return i
                            }
                            return 0
                        }
                    }

                    SpinBox {
                        Kirigami.FormData.label: i18n("Grid columns:")
                        from: 1
                        to: root.constants.zoneSelectorGridColumnsMax
                        value: kcm.zoneSelectorGridColumns
                        visible: kcm.zoneSelectorLayoutMode === 0
                        onValueModified: kcm.zoneSelectorGridColumns = value
                    }

                    SpinBox {
                        Kirigami.FormData.label: i18n("Max visible rows:")
                        from: 1
                        to: 10
                        value: kcm.zoneSelectorMaxRows
                        visible: kcm.zoneSelectorLayoutMode === 0  // Only applies to Grid mode
                        onValueModified: kcm.zoneSelectorMaxRows = value

                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: i18n("Scrolling enabled when more rows exist")
                    }
                }
            }
        }

        // Preview Size card - wrapped in Item for stable sizing
        Item {
            Layout.fillWidth: true
            implicitHeight: previewCard.implicitHeight

            Kirigami.Card {
                id: previewCard
                anchors.fill: parent
                enabled: kcm.zoneSelectorEnabled

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Preview Size")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.largeSpacing

                    // Live preview - centered
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.effectivePreviewHeight + 50

                        // Preview container
                        Item {
                            id: sizePreviewContainer
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            width: root.effectivePreviewWidth
                            height: root.effectivePreviewHeight

                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                radius: Kirigami.Units.smallSpacing * 1.5
                                border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4)
                                border.width: 1

                                // Sample zones
                                Row {
                                    anchors.fill: parent
                                    anchors.margins: 2
                                    spacing: 1

                                    Repeater {
                                        model: 3
                                        Rectangle {
                                            width: (parent.width - 2) / 3
                                            height: parent.height
                                            radius: 2
                                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.35)
                                            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.7)
                                            border.width: 1

                                            Label {
                                                anchors.centerIn: parent
                                                text: (index + 1).toString()
                                                font.pixelSize: Math.min(parent.width, parent.height) * 0.3
                                                font.bold: true
                                                color: Kirigami.Theme.textColor
                                                opacity: 0.6
                                                visible: parent.width >= 20
                                            }
                                        }
                                    }
                                }
                            }

                            // Size label
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.bottom
                                anchors.topMargin: Kirigami.Units.smallSpacing
                                text: root.effectivePreviewWidth + " × " + root.effectivePreviewHeight + " px"
                                font: Kirigami.Theme.fixedWidthFont
                                opacity: 0.7
                            }
                        }
                    }

                    // Size selection - segmented button style
                    RowLayout {
                        id: sizeButtonRow
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 0

                        // Track explicit Custom mode selection
                        property bool customModeActive: false

                        // Track which size is selected
                        // 0=Auto, 1=Small(120), 2=Medium(180), 3=Large(260), 4=Custom
                        property int selectedSize: {
                            if (kcm.zoneSelectorSizeMode === 0) return 0  // Auto
                            if (customModeActive) return 4  // Explicit Custom selection
                            var w = kcm.zoneSelectorPreviewWidth
                            if (Math.abs(w - 120) <= 5) return 1  // Small
                            if (Math.abs(w - 180) <= 5) return 2  // Medium
                            if (Math.abs(w - 260) <= 5) return 3  // Large
                            return 4  // Custom (width doesn't match preset)
                        }

                        Button {
                            text: i18n("Auto")
                            flat: parent.selectedSize !== 0
                            highlighted: parent.selectedSize === 0
                            onClicked: {
                                sizeButtonRow.customModeActive = false
                                kcm.zoneSelectorSizeMode = 0
                            }

                            ToolTip.visible: hovered
                            ToolTip.delay: Kirigami.Units.toolTipDelay
                            ToolTip.text: i18n("Approximately 10% of screen width (120-280px)")
                        }

                        Button {
                            text: i18n("Small")
                            flat: parent.selectedSize !== 1
                            highlighted: parent.selectedSize === 1
                            onClicked: {
                                sizeButtonRow.customModeActive = false
                                kcm.zoneSelectorSizeMode = 1
                                kcm.zoneSelectorPreviewWidth = 120
                                kcm.zoneSelectorPreviewHeight = Math.round(120 / root.screenAspectRatio)
                            }

                            ToolTip.visible: hovered
                            ToolTip.delay: Kirigami.Units.toolTipDelay
                            ToolTip.text: i18n("120px width")
                        }

                        Button {
                            text: i18n("Medium")
                            flat: parent.selectedSize !== 2
                            highlighted: parent.selectedSize === 2
                            onClicked: {
                                sizeButtonRow.customModeActive = false
                                kcm.zoneSelectorSizeMode = 1
                                kcm.zoneSelectorPreviewWidth = 180
                                kcm.zoneSelectorPreviewHeight = Math.round(180 / root.screenAspectRatio)
                            }

                            ToolTip.visible: hovered
                            ToolTip.delay: Kirigami.Units.toolTipDelay
                            ToolTip.text: i18n("180px width")
                        }

                        Button {
                            text: i18n("Large")
                            flat: parent.selectedSize !== 3
                            highlighted: parent.selectedSize === 3
                            onClicked: {
                                sizeButtonRow.customModeActive = false
                                kcm.zoneSelectorSizeMode = 1
                                kcm.zoneSelectorPreviewWidth = 260
                                kcm.zoneSelectorPreviewHeight = Math.round(260 / root.screenAspectRatio)
                            }

                            ToolTip.visible: hovered
                            ToolTip.delay: Kirigami.Units.toolTipDelay
                            ToolTip.text: i18n("260px width")
                        }

                        Button {
                            text: i18n("Custom")
                            flat: parent.selectedSize !== 4
                            highlighted: parent.selectedSize === 4
                            onClicked: {
                                sizeButtonRow.customModeActive = true
                                kcm.zoneSelectorSizeMode = 1
                            }

                            ToolTip.visible: hovered
                            ToolTip.delay: Kirigami.Units.toolTipDelay
                            ToolTip.text: i18n("Custom size with slider")
                        }
                    }

                    // Custom size slider - only visible when Custom is selected
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignHCenter
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 25
                        visible: sizeButtonRow.selectedSize === 4
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            text: i18n("Size:")
                        }

                        Slider {
                            id: customSizeSlider
                            Layout.fillWidth: true
                            from: root.constants.zoneSelectorPreviewWidthMin
                            to: root.constants.zoneSelectorPreviewWidthMax
                            value: kcm.zoneSelectorPreviewWidth
                            stepSize: 10
                            onMoved: {
                                kcm.zoneSelectorPreviewWidth = value
                                // Always maintain aspect ratio
                                var newHeight = Math.round(value / root.screenAspectRatio)
                                newHeight = Math.max(root.constants.zoneSelectorPreviewHeightMin, Math.min(root.constants.zoneSelectorPreviewHeightMax, newHeight))
                                kcm.zoneSelectorPreviewHeight = newHeight
                            }
                        }

                        Label {
                            text: kcm.zoneSelectorPreviewWidth + " px"
                            Layout.preferredWidth: 55
                            horizontalAlignment: Text.AlignRight
                            font: Kirigami.Theme.fixedWidthFont
                        }
                    }

                    // Info text for auto mode
                    Label {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignHCenter
                        visible: kcm.zoneSelectorSizeMode === 0
                        text: i18n("Preview size adjusts automatically based on your screen resolution.")
                        opacity: 0.6
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }

        // On-Screen Display card - wrapped in Item for stable sizing
        Item {
            Layout.fillWidth: true
            implicitHeight: osdCard.implicitHeight

            Kirigami.Card {
                id: osdCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("On-Screen Display")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        id: showOsdCheckbox
                        Kirigami.FormData.label: i18n("Layout switch:")
                        text: i18n("Show OSD when switching layouts")
                        checked: kcm.showOsdOnLayoutSwitch
                        onToggled: kcm.showOsdOnLayoutSwitch = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Keyboard navigation:")
                        text: i18n("Show OSD when using keyboard navigation")
                        checked: kcm.showNavigationOsd
                        onToggled: kcm.showNavigationOsd = checked
                    }

                    ComboBox {
                        id: osdStyleCombo
                        Kirigami.FormData.label: i18n("OSD style:")
                        enabled: showOsdCheckbox.checked || kcm.showNavigationOsd

                        readonly property int osdStyleNone: 0
                        readonly property int osdStyleText: 1
                        readonly property int osdStylePreview: 2

                        currentIndex: Math.max(0, kcm.osdStyle - osdStyleText)
                        model: [
                            i18n("Text only"),
                            i18n("Visual preview")
                        ]
                        onActivated: (index) => {
                            kcm.osdStyle = index + osdStyleText
                        }
                    }
                }
            }
        }
    }
}
