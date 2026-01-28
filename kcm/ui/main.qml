// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @brief Main KCM settings UI with tabbed interface
 *
 * Pages:
 * - Layouts: View, create, edit, import/export layouts
 * - Appearance: Color and style customization
 * - Behavior: Window snapping options
 * - Exclusions: Apps and windows to exclude from snapping
 * - Editor: Keyboard shortcuts and snapping settings
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.AbstractKCM {
    id: root

    title: i18n("PlasmaZones")
    framedView: false

    // Screen aspect ratio for locked preview calculations
    readonly property real screenAspectRatio: Screen.width > 0 && Screen.height > 0
        ? (Screen.width / Screen.height)
        : (16.0 / 9.0)

    // Effective preview dimensions (matches overlayservice.cpp auto-sizing logic)
    // Auto mode: width = clamp(screenWidth/10, 120, 280), height = width/aspectRatio
    // Manual mode: use settings values
    readonly property int effectivePreviewWidth: kcm.zoneSelectorSizeMode === 0
        ? Math.max(120, Math.min(280, Math.round(Screen.width / 10)))
        : kcm.zoneSelectorPreviewWidth
    readonly property int effectivePreviewHeight: kcm.zoneSelectorSizeMode === 0
        ? Math.round(effectivePreviewWidth / screenAspectRatio)
        : kcm.zoneSelectorPreviewHeight

    // Constants to eliminate magic numbers
    QtObject {
        id: constants

        // Layout dimensions
        readonly property int layoutListMinHeight: 150
        readonly property int sliderPreferredWidth: 200
        readonly property int sliderValueLabelWidth: 40
        readonly property int colorButtonSize: 32

        // Opacity values
        readonly property real labelSecondaryOpacity: 0.7

        // Slider ranges
        readonly property int opacitySliderMax: 100
        readonly property int borderWidthMax: 10
        readonly property int borderRadiusMax: 50
        readonly property int paddingMax: 100
        readonly property int thresholdMax: 50
        readonly property int zoneSelectorTriggerMax: 200
        readonly property int zoneSelectorPreviewWidthMin: 80
        readonly property int zoneSelectorPreviewWidthMax: 400
        readonly property int zoneSelectorPreviewHeightMin: 60
        readonly property int zoneSelectorPreviewHeightMax: 300
        readonly property int zoneSelectorGridColumnsMax: 10

        // Layout type ratios (matching C++ Defaults)
        readonly property real priorityGridMainRatio: 0.67
        readonly property real priorityGridSecondaryRatio: 0.33
        readonly property real focusSideRatio: 0.15
        readonly property real focusMainRatio: 0.70
    }

    header: ColumnLayout {
        width: parent.width
        spacing: 0

        // Master enable/disable row
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing

            Label {
                text: i18n("Enable PlasmaZones")
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Label {
                text: kcm.daemonRunning ? i18n("Running") : i18n("Stopped")
                opacity: 0.7
            }

            Switch {
                id: daemonEnabledSwitch
                checked: kcm.daemonEnabled
                onToggled: kcm.daemonEnabled = checked
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: kcm.daemonEnabled
        }

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            visible: kcm.daemonEnabled

            TabButton {
                text: i18n("Layouts")
                icon.name: "view-grid-symbolic"
            }
            TabButton {
                text: i18n("Editor")
                icon.name: "document-edit"
            }
            TabButton {
                text: i18n("Assignments")
                icon.name: "view-list-details"
            }
            TabButton {
                text: i18n("Appearance")
                icon.name: "preferences-desktop-color"
            }
            TabButton {
                text: i18n("Behavior")
                icon.name: "preferences-system-windows-behavior"
            }
            TabButton {
                text: i18n("Zone Selector")
                icon.name: "select-rectangular"
            }
            TabButton {
                text: i18n("Exclusions")
                icon.name: "dialog-cancel-symbolic"
            }
        }
    }

    // Placeholder when daemon is disabled
    Item {
        anchors.fill: parent
        visible: !kcm.daemonEnabled

        ColumnLayout {
            anchors.centerIn: parent
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Icon {
                source: "plasmazones"
                Layout.preferredWidth: Kirigami.Units.iconSizes.huge
                Layout.preferredHeight: Kirigami.Units.iconSizes.huge
                Layout.alignment: Qt.AlignHCenter
                opacity: 0.5
            }

            Label {
                text: i18n("PlasmaZones is disabled")
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: i18n("Turn on PlasmaZones to manage window layouts.")
                opacity: 0.7
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }

    StackLayout {
        id: stackLayout
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        currentIndex: tabBar.currentIndex
        visible: kcm.daemonEnabled

        // ═══════════════════════════════════════════════════════════════════
        // TAB 1: LAYOUTS
        // ═══════════════════════════════════════════════════════════════════
        ColumnLayout {
            visible: stackLayout.currentIndex === 0
            spacing: Kirigami.Units.largeSpacing

            // Layout actions toolbar
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Button {
                    text: i18n("New Layout")
                    icon.name: "list-add"
                    onClicked: kcm.createNewLayout()
                }

                Button {
                    text: i18n("Edit")
                    icon.name: "document-edit"
                    enabled: layoutList.currentItem !== null
                    onClicked: {
                        if (layoutList.currentItem) {
                            kcm.editLayout(layoutList.currentItem.modelData.id)
                        }
                    }
                }

                Button {
                    text: i18n("Duplicate")
                    icon.name: "edit-copy"
                    enabled: layoutList.currentItem !== null
                    onClicked: {
                        if (layoutList.currentItem) {
                            kcm.duplicateLayout(layoutList.currentItem.modelData.id)
                        }
                    }
                }

                Button {
                    text: i18n("Delete")
                    icon.name: "edit-delete"
                    enabled: layoutList.currentItem !== null && !layoutList.currentItem.modelData.isSystem
                    onClicked: {
                        if (layoutList.currentItem) {
                            deleteConfirmDialog.layoutToDelete = layoutList.currentItem.modelData
                            deleteConfirmDialog.open()
                        }
                    }
                    ToolTip.visible: hovered && stackLayout.currentIndex === 0
                    ToolTip.text: i18n("Delete the selected layout")
                }

                Button {
                    text: i18n("Set as Default")
                    icon.name: "favorite"
                    enabled: layoutList.currentItem !== null && layoutList.currentItem.modelData.id !== kcm.defaultLayoutId
                    onClicked: {
                        if (layoutList.currentItem) {
                            kcm.defaultLayoutId = layoutList.currentItem.modelData.id
                        }
                    }
                    ToolTip.visible: hovered && stackLayout.currentIndex === 0
                    ToolTip.text: i18n("Set this layout as the default for screens without specific assignments")
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: i18n("Import")
                    icon.name: "document-import"
                    onClicked: importDialog.open()
                }

                Button {
                    text: i18n("Export")
                    icon.name: "document-export"
                    enabled: layoutList.currentItem !== null
                    onClicked: {
                        if (layoutList.currentItem) {
                            exportDialog.layoutId = layoutList.currentItem.modelData.id
                            exportDialog.open()
                        }
                    }
                }
            }

            // Layout grid - fills remaining space with responsive columns
            GridView {
                id: layoutList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: constants.layoutListMinHeight
                model: kcm.layouts
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                focus: true  // Enable keyboard navigation
                keyNavigationEnabled: true
                
                // Calculate responsive cell size - aim for 2-4 columns
                readonly property real minCellWidth: Kirigami.Units.gridUnit * 14  // ~112px minimum per cell
                readonly property real maxCellWidth: Kirigami.Units.gridUnit * 22  // ~176px maximum per cell
                readonly property int columnCount: Math.max(2, Math.floor(width / minCellWidth))
                readonly property real actualCellWidth: width / columnCount
                
                cellWidth: actualCellWidth
                cellHeight: Kirigami.Units.gridUnit * 10  // ~80px height for thumbnail + info
                
                // Function to select layout by ID
                function selectLayoutById(layoutId) {
                    if (!layoutId || !kcm.layouts) {
                        return false
                    }

                    // Search directly in the model array, comparing as strings
                    for (let i = 0; i < kcm.layouts.length; i++) {
                        const layout = kcm.layouts[i]
                        if (layout && String(layout.id) === String(layoutId)) {
                            currentIndex = i
                            positionViewAtIndex(i, GridView.Contain)
                            return true  // Found and selected
                        }
                    }
                    return false  // Not found
                }
                
                // Handle layout selection when selection target changes
                Connections {
                    target: kcm
                    function onLayoutToSelectChanged() {
                        // Select layout when target changes
                        // Use callLater to ensure model is ready if it just updated
                        if (kcm.layoutToSelect) {
                            Qt.callLater(() => {
                                layoutList.selectLayoutById(kcm.layoutToSelect)
                            })
                        }
                    }
                }

                // Handle when count changes (model items are ready after update)
                onCountChanged: {
                    if (kcm.layoutToSelect && count > 0) {
                        layoutList.selectLayoutById(kcm.layoutToSelect)
                    }
                }

                // Select the active layout when component first loads
                Component.onCompleted: {
                    // Use a small delay to ensure the model is fully populated
                    // and layoutToSelect has been set from the daemon
                    Qt.callLater(() => {
                        if (kcm.layoutToSelect && count > 0) {
                            selectLayoutById(kcm.layoutToSelect)
                        }
                    })
                }

                // Add a background card effect
                Rectangle {
                    anchors.fill: parent
                    z: -1
                    color: Kirigami.Theme.backgroundColor
                    border.color: Kirigami.Theme.disabledTextColor
                    border.width: 1
                    radius: Kirigami.Units.smallSpacing
                }

                delegate: Item {
                    id: layoutDelegate
                    width: layoutList.cellWidth
                    height: layoutList.cellHeight
                    
                    required property var modelData
                    required property int index
                    
                    property bool isSelected: GridView.isCurrentItem
                    property bool isHovered: hoverHandler.hovered
                    
                    // Accessibility
                    Accessible.name: modelData.name || i18n("Unnamed Layout")
                    Accessible.description: modelData.isSystem 
                        ? i18n("System layout with %1 zones", modelData.zoneCount || 0)
                        : i18n("Custom layout with %1 zones", modelData.zoneCount || 0)
                    Accessible.role: Accessible.ListItem

                    // HoverHandler for hover detection (doesn't block events)
                    HoverHandler {
                        id: hoverHandler
                    }
                    
                    // TapHandler for click/double-click (doesn't block other handlers)
                    TapHandler {
                        onTapped: layoutList.currentIndex = index
                        onDoubleTapped: {
                            // All layouts can be edited - changes save to user directory
                            kcm.editLayout(modelData.id)
                        }
                    }
                    
                    // Keyboard support: Enter to edit, Delete to delete
                    Keys.onReturnPressed: {
                        // All layouts can be edited - changes save to user directory
                        kcm.editLayout(modelData.id)
                    }
                    Keys.onDeletePressed: {
                        if (!modelData.isSystem) {
                            deleteConfirmDialog.layoutToDelete = modelData
                            deleteConfirmDialog.open()
                        }
                    }

                    // Card container with padding
                    Rectangle {
                        id: cardBackground
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.smallSpacing / 2
                        radius: Kirigami.Units.smallSpacing
                        color: layoutDelegate.isSelected ? Kirigami.Theme.highlightColor : 
                               layoutDelegate.isHovered ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2) :
                               "transparent"
                        border.color: layoutDelegate.isSelected ? Kirigami.Theme.highlightColor :
                                      layoutDelegate.isHovered ? Kirigami.Theme.disabledTextColor :
                                      "transparent"
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing
                            spacing: Kirigami.Units.smallSpacing / 2

                            // Layout preview thumbnail - centered
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                
                                LayoutThumbnail {
                                    id: layoutThumbnail
                                    anchors.centerIn: parent
                                    layout: modelData
                                    isSelected: layoutDelegate.isSelected
                                    // Scale to fit available space (guard against division by zero during layout)
                                    scale: (implicitWidth > 0 && implicitHeight > 0 && parent.width > 0 && parent.height > 0)
                                           ? Math.min(1, Math.min(parent.width / implicitWidth, parent.height / implicitHeight))
                                           : 1
                                    transformOrigin: Item.Center
                                }
                                
                                // Default layout star (top-left)
                                Kirigami.Icon {
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.margins: Kirigami.Units.smallSpacing
                                    source: "favorite"
                                    visible: modelData.id === kcm.defaultLayoutId
                                    width: Kirigami.Units.iconSizes.small
                                    height: Kirigami.Units.iconSizes.small
                                    color: Kirigami.Theme.positiveTextColor
                                    
                                    HoverHandler { id: defaultIconHover }
                                    ToolTip.visible: defaultIconHover.hovered
                                    ToolTip.text: i18n("Default layout")
                                }
                            }

                            // Layout info row (compact)
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing / 2
                                
                                Label {
                                    text: {
                                        var zoneCount = modelData.zoneCount || 0
                                        if (modelData.isSystem) {
                                            return i18n("System • %1", zoneCount)
                                        } else {
                                            return i18n("%1 zones", zoneCount)
                                        }
                                    }
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    color: layoutDelegate.isSelected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                                }
                            }
                        }
                    }
                    
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 4
                    visible: layoutList.count === 0
                    text: i18n("No layouts available")
                    explanation: i18n("Start the PlasmaZones daemon or create a new layout")
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // TAB 2: EDITOR
        // ═══════════════════════════════════════════════════════════════════
        ScrollView {
            visible: stackLayout.currentIndex === 1
            clip: true
            contentWidth: availableWidth

            Kirigami.FormLayout {
                width: parent.width

                // Editor shortcuts section
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Keyboard Shortcuts")
                }

                KeySequenceInput {
                    id: editorDuplicateShortcutField
                    Kirigami.FormData.label: i18n("Duplicate zone:")
                    keySequence: kcm.editorDuplicateShortcut
                    onKeySequenceModified: (sequence) => {
                        kcm.editorDuplicateShortcut = sequence
                    }
                    ToolTip.visible: hovered && stackLayout.currentIndex === 1
                    ToolTip.text: i18n("Keyboard shortcut to duplicate the selected zone. Click and press keys.")
                }

                KeySequenceInput {
                    id: editorSplitHorizontalShortcutField
                    Kirigami.FormData.label: i18n("Split horizontally:")
                    keySequence: kcm.editorSplitHorizontalShortcut
                    onKeySequenceModified: (sequence) => {
                        kcm.editorSplitHorizontalShortcut = sequence
                    }
                    ToolTip.visible: hovered && stackLayout.currentIndex === 1
                    ToolTip.text: i18n("Keyboard shortcut to split selected zone horizontally. Click and press keys.")
                }

                KeySequenceInput {
                    id: editorSplitVerticalShortcutField
                    Kirigami.FormData.label: i18n("Split vertically:")
                    keySequence: kcm.editorSplitVerticalShortcut
                    onKeySequenceModified: (sequence) => {
                        kcm.editorSplitVerticalShortcut = sequence
                    }
                    ToolTip.visible: hovered && stackLayout.currentIndex === 1
                    ToolTip.text: i18n("Keyboard shortcut to split selected zone vertically. Click and press keys.")
                }

                KeySequenceInput {
                    id: editorFillShortcutField
                    Kirigami.FormData.label: i18n("Fill space:")
                    keySequence: kcm.editorFillShortcut
                    onKeySequenceModified: (sequence) => {
                        kcm.editorFillShortcut = sequence
                    }
                    ToolTip.visible: hovered && stackLayout.currentIndex === 1
                    ToolTip.text: i18n("Keyboard shortcut to expand selected zone to fill available space. Click and press keys.")
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Reset shortcuts:")
                    spacing: Kirigami.Units.smallSpacing

                    Button {
                        text: i18n("Reset to Defaults")
                        icon.name: "edit-reset"
                        onClicked: {
                            // Reset all editor shortcuts to defaults
                            kcm.resetEditorShortcuts()
                        }
                        ToolTip.visible: hovered && stackLayout.currentIndex === 1
                        ToolTip.text: i18n("Reset all editor shortcuts to their default values")
                    }
                }

                // Editor snapping section
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Snapping")
                }

                CheckBox {
                    Kirigami.FormData.label: i18n("Grid snapping:")
                    text: i18n("Enable grid snapping")
                    checked: kcm.editorGridSnappingEnabled
                    onToggled: kcm.editorGridSnappingEnabled = checked
                    ToolTip.visible: hovered && stackLayout.currentIndex === 1
                    ToolTip.text: i18n("Snap zones to a grid while dragging or resizing")
                }

                CheckBox {
                    Kirigami.FormData.label: i18n("Edge snapping:")
                    text: i18n("Enable edge snapping")
                    checked: kcm.editorEdgeSnappingEnabled
                    onToggled: kcm.editorEdgeSnappingEnabled = checked
                    ToolTip.visible: hovered && stackLayout.currentIndex === 1
                    ToolTip.text: i18n("Snap zones to edges of other zones while dragging or resizing")
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Grid interval X:")
                    spacing: Kirigami.Units.smallSpacing

                    Slider {
                        id: snapIntervalXSlider
                        Layout.preferredWidth: constants.sliderPreferredWidth
                        from: 0.01
                        to: 0.5
                        stepSize: 0.01
                        value: kcm.editorSnapIntervalX
                        onMoved: kcm.editorSnapIntervalX = value
                    }

                    Label {
                        text: Math.round(snapIntervalXSlider.value * 100) + "%"
                        Layout.preferredWidth: constants.sliderValueLabelWidth
                    }
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Grid interval Y:")
                    spacing: Kirigami.Units.smallSpacing

                    Slider {
                        id: snapIntervalYSlider
                        Layout.preferredWidth: constants.sliderPreferredWidth
                        from: 0.01
                        to: 0.5
                        stepSize: 0.01
                        value: kcm.editorSnapIntervalY
                        onMoved: kcm.editorSnapIntervalY = value
                    }

                    Label {
                        text: Math.round(snapIntervalYSlider.value * 100) + "%"
                        Layout.preferredWidth: constants.sliderValueLabelWidth
                    }
                }

                ModifierCheckBoxes {
                    id: snapOverrideModifiers
                    Kirigami.FormData.label: i18n("Snap override modifiers:")
                    modifierValue: kcm.editorSnapOverrideModifier
                    tooltipEnabled: stackLayout.currentIndex === 1
                    onValueModified: (value) => {
                        kcm.editorSnapOverrideModifier = value
                    }
                }

                // Fill on Drop section
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Fill on Drop")
                }

                CheckBox {
                    id: fillOnDropEnabledCheck
                    Kirigami.FormData.label: i18n("Enable:")
                    text: i18n("Fill zone on drop with modifier key")
                    checked: kcm.fillOnDropEnabled
                    onToggled: kcm.fillOnDropEnabled = checked
                    ToolTip.visible: hovered && stackLayout.currentIndex === 1
                    ToolTip.text: i18n("When enabled, holding the modifier key while dropping a zone expands it to fill available space")
                }

                ModifierCheckBoxes {
                    id: fillOnDropModifiers
                    Kirigami.FormData.label: i18n("Fill on drop modifiers:")
                    enabled: fillOnDropEnabledCheck.checked
                    modifierValue: kcm.fillOnDropModifier
                    tooltipEnabled: stackLayout.currentIndex === 1
                    onValueModified: (value) => {
                        kcm.fillOnDropModifier = value
                    }
                }

            }

            // Keep key sequence inputs in sync with KCM properties
            Connections {
                target: kcm
                function onEditorSaveShortcutChanged() {
                    if (!editorSaveShortcutField.capturing) {
                        editorSaveShortcutField.keySequence = kcm.editorSaveShortcut
                    }
                }
                function onEditorDeleteShortcutChanged() {
                    if (!editorDeleteShortcutField.capturing) {
                        editorDeleteShortcutField.keySequence = kcm.editorDeleteShortcut
                    }
                }
                function onEditorDuplicateShortcutChanged() {
                    if (!editorDuplicateShortcutField.capturing) {
                        editorDuplicateShortcutField.keySequence = kcm.editorDuplicateShortcut
                    }
                }
                function onEditorCloseShortcutChanged() {
                    if (!editorCloseShortcutField.capturing) {
                        editorCloseShortcutField.keySequence = kcm.editorCloseShortcut
                    }
                }
                function onEditorSplitHorizontalShortcutChanged() {
                    if (!editorSplitHorizontalShortcutField.capturing) {
                        editorSplitHorizontalShortcutField.keySequence = kcm.editorSplitHorizontalShortcut
                    }
                }
                function onEditorSplitVerticalShortcutChanged() {
                    if (!editorSplitVerticalShortcutField.capturing) {
                        editorSplitVerticalShortcutField.keySequence = kcm.editorSplitVerticalShortcut
                    }
                }
                function onEditorFillShortcutChanged() {
                    if (!editorFillShortcutField.capturing) {
                        editorFillShortcutField.keySequence = kcm.editorFillShortcut
                    }
                }
                function onEditorSnapIntervalXChanged() {
                    if (!snapIntervalXSlider.pressed) {
                        snapIntervalXSlider.value = kcm.editorSnapIntervalX
                    }
                }
                function onEditorSnapIntervalYChanged() {
                    if (!snapIntervalYSlider.pressed) {
                        snapIntervalYSlider.value = kcm.editorSnapIntervalY
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // TAB 3: ASSIGNMENTS
        // ═══════════════════════════════════════════════════════════════════
        ScrollView {
            visible: stackLayout.currentIndex === 2
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: Kirigami.Units.largeSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    type: Kirigami.MessageType.Information
                    text: i18n("Assign different layouts to each monitor and configure quick-switch keyboard shortcuts.")
                    visible: true
                }

                // Monitor Assignments Card
                Kirigami.Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: false

                    header: Kirigami.Heading {
                        level: 3
                        text: i18n("Monitor Assignments")
                        padding: Kirigami.Units.smallSpacing
                    }

                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing

                        ListView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: count > 0 ? contentHeight : Kirigami.Units.gridUnit * 4
                            Layout.margins: Kirigami.Units.smallSpacing
                            clip: true
                            model: kcm.screens
                            interactive: false

                            delegate: Item {
                                id: monitorDelegate
                                width: ListView.view.width
                                height: monitorContent.implicitHeight
                                required property var modelData
                                required property int index

                                property bool expanded: false
                                property string screenName: modelData.name || ""

                                ColumnLayout {
                                    id: monitorContent
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.leftMargin: Kirigami.Units.smallSpacing
                                    anchors.rightMargin: Kirigami.Units.smallSpacing
                                    spacing: Kirigami.Units.smallSpacing

                                    // Top spacer
                                    Item { height: Kirigami.Units.smallSpacing }

                                    // Header row - always visible
                                    RowLayout {
                                        id: monitorHeaderRow
                                        Layout.fillWidth: true
                                        spacing: Kirigami.Units.smallSpacing

                                        Kirigami.Icon {
                                            source: "video-display"
                                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                            Layout.alignment: Qt.AlignTop
                                        }

                                        ColumnLayout {
                                            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                                            spacing: 0

                                            Label {
                                                id: monitorNameLabel
                                                text: modelData.name || i18n("Unknown Monitor")
                                                font.bold: true
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }

                                            Label {
                                                text: {
                                                    let info = modelData.resolution || ""
                                                    if (modelData.isPrimary) {
                                                        info += (info ? " • " : "") + i18n("Primary")
                                                    }
                                                    return info
                                                }
                                                opacity: constants.labelSecondaryOpacity
                                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                        }

                                        // "All Desktops" label
                                        Label {
                                            text: i18n("All Desktops:")
                                            Layout.alignment: Qt.AlignVCenter
                                        }

                                        ComboBox {
                                            id: screenLayoutCombo
                                            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                                            model: {
                                                let items = [{text: i18n("Default"), value: "", layout: null}]
                                                for (let i = 0; i < kcm.layouts.length; i++) {
                                                    items.push({
                                                        text: kcm.layouts[i].name,
                                                        value: kcm.layouts[i].id,
                                                        layout: kcm.layouts[i]
                                                    })
                                                }
                                                return items
                                            }
                                            textRole: "text"
                                            valueRole: "value"

                                            function updateFromAssignment() {
                                                let assignedId = kcm.getLayoutForScreen(monitorDelegate.screenName)
                                                if (assignedId && assignedId !== "") {
                                                    for (let i = 0; i < model.length; i++) {
                                                        if (model[i].value === assignedId) {
                                                            currentIndex = i
                                                            return
                                                        }
                                                    }
                                                }
                                                currentIndex = 0
                                            }

                                            Component.onCompleted: updateFromAssignment()

                                            Connections {
                                                target: kcm
                                                function onScreenAssignmentsChanged() {
                                                    screenLayoutCombo.updateFromAssignment()
                                                }
                                            }

                                            onActivated: {
                                                let selectedValue = model[currentIndex].value
                                                if (selectedValue === "") {
                                                    kcm.clearScreenAssignment(monitorDelegate.screenName)
                                                } else {
                                                    kcm.assignLayoutToScreen(monitorDelegate.screenName, selectedValue)
                                                }
                                            }
                                        }

                                        ToolButton {
                                            icon.name: "edit-clear"
                                            onClicked: {
                                                kcm.clearScreenAssignment(monitorDelegate.screenName)
                                                screenLayoutCombo.currentIndex = 0
                                            }
                                            ToolTip.visible: hovered
                                            ToolTip.text: i18n("Clear assignment")
                                        }

                                        Item { Layout.fillWidth: true }

                                        // Expand button - only show if multiple virtual desktops
                                        ToolButton {
                                            visible: kcm.virtualDesktopCount > 1
                                            icon.name: monitorDelegate.expanded ? "go-up" : "go-down"
                                            text: monitorDelegate.expanded ? "" : i18n("Per-desktop")
                                            display: AbstractButton.TextBesideIcon
                                            onClicked: monitorDelegate.expanded = !monitorDelegate.expanded
                                            ToolTip.visible: hovered
                                            ToolTip.text: monitorDelegate.expanded ?
                                                i18n("Hide per-desktop assignments") :
                                                i18n("Show per-desktop assignments")
                                        }
                                    }

                                    // Disable PlasmaZones on this monitor (no overlay, zone picker, or snapping)
                                    CheckBox {
                                        Layout.fillWidth: true
                                        text: i18n("Disable PlasmaZones on this monitor")
                                        checked: kcm.disabledMonitors.indexOf(monitorDelegate.screenName) >= 0
                                        onToggled: kcm.setMonitorDisabled(monitorDelegate.screenName, checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: i18n("When enabled, the zone overlay and zone picker will not appear on this monitor, and windows will not snap to zones here.")
                                    }

                                    // Per-desktop assignments section - expandable
                                    ColumnLayout {
                                        id: perDesktopSection
                                        Layout.fillWidth: true
                                        Layout.leftMargin: Kirigami.Units.gridUnit * 2
                                        visible: monitorDelegate.expanded && kcm.virtualDesktopCount > 1
                                        spacing: Kirigami.Units.smallSpacing

                                        Kirigami.Separator {
                                            Layout.fillWidth: true
                                        }

                                        Label {
                                            text: i18n("Per-Desktop Overrides")
                                            font.bold: true
                                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                                            opacity: 0.8
                                        }

                                        Label {
                                            text: i18n("Override the default layout for specific virtual desktops")
                                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                                            opacity: constants.labelSecondaryOpacity
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }

                                        // Per-desktop combo boxes
                                        Repeater {
                                            model: kcm.virtualDesktopCount

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: Kirigami.Units.smallSpacing

                                                required property int index

                                                // Desktop number is 1-based for display but index is 0-based
                                                property int desktopNumber: index + 1
                                                property string desktopName: kcm.virtualDesktopNames[index] || i18n("Desktop %1", desktopNumber)

                                                Kirigami.Icon {
                                                    source: "preferences-desktop-virtual"
                                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                                    Layout.alignment: Qt.AlignVCenter
                                                }

                                                Label {
                                                    text: desktopName
                                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                                    Layout.alignment: Qt.AlignVCenter
                                                    elide: Text.ElideRight
                                                }

                                                ComboBox {
                                                    id: desktopLayoutCombo
                                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 12

                                                    property int desktopNum: desktopNumber

                                                    model: {
                                                        let items = [{text: i18n("Use default"), value: "", layout: null}]
                                                        for (let i = 0; i < kcm.layouts.length; i++) {
                                                            items.push({
                                                                text: kcm.layouts[i].name,
                                                                value: kcm.layouts[i].id,
                                                                layout: kcm.layouts[i]
                                                            })
                                                        }
                                                        return items
                                                    }
                                                    textRole: "text"
                                                    valueRole: "value"

                                                    function updateFromAssignment() {
                                                        // Check if there's an EXPLICIT assignment (not inherited from fallback)
                                                        let hasExplicit = kcm.hasExplicitAssignmentForScreenDesktop(monitorDelegate.screenName, desktopNum)
                                                        if (!hasExplicit) {
                                                            // No explicit assignment - show "Use default"
                                                            currentIndex = 0
                                                            return
                                                        }
                                                        // Has explicit assignment - find and select it
                                                        let assignedId = kcm.getLayoutForScreenDesktop(monitorDelegate.screenName, desktopNum)
                                                        if (assignedId && assignedId !== "") {
                                                            for (let i = 0; i < model.length; i++) {
                                                                if (model[i].value === assignedId) {
                                                                    currentIndex = i
                                                                    return
                                                                }
                                                            }
                                                        }
                                                        currentIndex = 0
                                                    }

                                                    Component.onCompleted: updateFromAssignment()

                                                    Connections {
                                                        target: kcm
                                                        function onScreenAssignmentsChanged() {
                                                            desktopLayoutCombo.updateFromAssignment()
                                                        }
                                                    }

                                                    onActivated: {
                                                        let selectedValue = model[currentIndex].value
                                                        if (selectedValue === "") {
                                                            kcm.clearScreenDesktopAssignment(monitorDelegate.screenName, desktopNum)
                                                        } else {
                                                            kcm.assignLayoutToScreenDesktop(monitorDelegate.screenName, desktopNum, selectedValue)
                                                        }
                                                    }
                                                }

                                                ToolButton {
                                                    icon.name: "edit-clear"
                                                    onClicked: {
                                                        kcm.clearScreenDesktopAssignment(monitorDelegate.screenName, desktopLayoutCombo.desktopNum)
                                                        desktopLayoutCombo.currentIndex = 0
                                                    }
                                                    ToolTip.visible: hovered
                                                    ToolTip.text: i18n("Clear assignment for this desktop")
                                                }

                                                Item { Layout.fillWidth: true }
                                            }
                                        }
                                    }

                                    // Bottom spacer for consistent padding
                                    Item { height: Kirigami.Units.smallSpacing }
                                }
                            }

                            Kirigami.PlaceholderMessage {
                                anchors.centerIn: parent
                                width: parent.width - Kirigami.Units.gridUnit * 4
                                visible: parent.count === 0
                                text: i18n("No monitors detected")
                                explanation: i18n("Make sure the PlasmaZones daemon is running")
                            }
                        }

                        // Info message when only one virtual desktop
                        Kirigami.InlineMessage {
                            Layout.fillWidth: true
                            Layout.margins: Kirigami.Units.smallSpacing
                            visible: kcm.virtualDesktopCount <= 1 && kcm.screens.length > 0
                            type: Kirigami.MessageType.Information
                            text: i18n("Per-desktop layout assignments are available when using multiple virtual desktops. Add more desktops in System Settings → Virtual Desktops.")
                        }
                    }
                }

                // Quick Layout Slots Card
                Kirigami.Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: false

                    header: Kirigami.Heading {
                        level: 3
                        text: i18n("Quick Layout Shortcuts")
                        padding: Kirigami.Units.smallSpacing
                    }

                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            Layout.fillWidth: true
                            Layout.margins: Kirigami.Units.smallSpacing
                            text: i18n("Assign layouts to keyboard shortcuts for instant switching.")
                            wrapMode: Text.WordWrap
                            opacity: constants.labelSecondaryOpacity
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: contentHeight
                            Layout.margins: Kirigami.Units.smallSpacing
                            clip: true
                            model: 9
                            interactive: false

                            delegate: Item {
                                width: ListView.view.width
                                height: shortcutRowLayout.implicitHeight + Kirigami.Units.smallSpacing * 2
                                required property int index

                                RowLayout {
                                    id: shortcutRowLayout
                                    anchors.fill: parent
                                    anchors.margins: Kirigami.Units.smallSpacing
                                    spacing: Kirigami.Units.smallSpacing

                                    Label {
                                        property string shortcut: kcm.getQuickLayoutShortcut(index + 1)
                                        text: shortcut !== "" ? shortcut : i18n("Not assigned")
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 14 + Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                                        font.family: "monospace"
                                        opacity: shortcut !== "" ? 1.0 : 0.6
                                    }

                                    Item { Layout.fillWidth: true }

                                    ComboBox {
                                        id: slotLayoutCombo
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 16

                                        // Store the shortcut slot number for this delegate
                                        property int slotNumber: index + 1

                                        model: {
                                            let items = [{text: i18n("None"), value: "", layout: null}]
                                            for (let i = 0; i < kcm.layouts.length; i++) {
                                                items.push({
                                                    text: kcm.layouts[i].name,
                                                    value: kcm.layouts[i].id,
                                                    layout: kcm.layouts[i]
                                                })
                                            }
                                            return items
                                        }
                                        textRole: "text"
                                        valueRole: "value"

                                        // Custom delegate for popup items with layout preview
                                        delegate: ItemDelegate {
                                            width: slotLayoutCombo.popup.width
                                            highlighted: slotLayoutCombo.highlightedIndex === index

                                            required property var modelData
                                            required property int index

                                            contentItem: RowLayout {
                                                spacing: Kirigami.Units.smallSpacing

                                                // Mini layout preview
                                                Rectangle {
                                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                                                    Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                                                    radius: Kirigami.Units.smallSpacing / 2
                                                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                                                    border.color: highlighted ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                                                    border.width: highlighted ? 2 : 1
                                                    visible: modelData.layout != null

                                                    Item {
                                                        id: zonePreviewContainer
                                                        anchors.fill: parent
                                                        anchors.margins: Math.round(Kirigami.Units.smallSpacing * 0.75)

                                                        property var zones: modelData.layout?.zones || []

                                                        Repeater {
                                                            model: zonePreviewContainer.zones
                                                            Rectangle {
                                                                required property var modelData
                                                                required property int index
                                                                property var relGeo: modelData.relativeGeometry || {}
                                                                x: (relGeo.x || 0) * zonePreviewContainer.width
                                                                y: (relGeo.y || 0) * zonePreviewContainer.height
                                                                width: Math.max(2, (relGeo.width || 0.25) * zonePreviewContainer.width)
                                                                height: Math.max(2, (relGeo.height || 1) * zonePreviewContainer.height)
                                                                // Match LayoutThumbnail colors for consistency
                                                                color: highlighted ?
                                                                    Qt.rgba(Kirigami.Theme.highlightedTextColor.r, Kirigami.Theme.highlightedTextColor.g, Kirigami.Theme.highlightedTextColor.b, 0.85) :
                                                                    Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
                                                                border.color: highlighted ?
                                                                    Kirigami.Theme.highlightedTextColor :
                                                                    Kirigami.Theme.highlightColor
                                                                border.width: Math.round(Kirigami.Units.devicePixelRatio * 2)
                                                                radius: Kirigami.Units.smallSpacing * 0.5
                                                            }
                                                        }
                                                    }
                                                }

                                                // "None" placeholder - opposite colors (starts visible, highlighted contrasts)
                                                Rectangle {
                                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                                                    Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                                                    radius: Kirigami.Units.smallSpacing / 2
                                                    color: highlighted ?
                                                        Qt.rgba(Kirigami.Theme.highlightedTextColor.r, Kirigami.Theme.highlightedTextColor.g, Kirigami.Theme.highlightedTextColor.b, 0.15) :
                                                        Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15)
                                                    border.color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.highlightColor
                                                    border.width: Math.round(Kirigami.Units.devicePixelRatio * 2)
                                                    visible: modelData.layout == null

                                                    Kirigami.Icon {
                                                        anchors.centerIn: parent
                                                        source: "action-unavailable-symbolic"
                                                        width: Kirigami.Units.iconSizes.smallMedium
                                                        height: Kirigami.Units.iconSizes.smallMedium
                                                        color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.highlightColor
                                                        opacity: 0.7
                                                    }
                                                }

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 0

                                                    Label {
                                                        text: modelData.text
                                                        font.bold: highlighted
                                                        color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }

                                                    Label {
                                                        text: modelData.layout ? i18n("%1 zones", modelData.layout.zoneCount || 0) : i18n("No shortcut assigned")
                                                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                                                        color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                                                        opacity: 0.7
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                            }
                                        }

                                        function updateFromSlot() {
                                            let assignedId = kcm.getQuickLayoutSlot(slotNumber)
                                            if (assignedId && assignedId !== "") {
                                                for (let i = 0; i < model.length; i++) {
                                                    if (model[i].value === assignedId) {
                                                        currentIndex = i
                                                        return
                                                    }
                                                }
                                            }
                                            currentIndex = 0
                                        }

                                        Component.onCompleted: updateFromSlot()

                                        Connections {
                                            target: kcm
                                            // Quick layout slots are also cleared when defaults are reset
                                            function onScreenAssignmentsChanged() {
                                                slotLayoutCombo.updateFromSlot()
                                            }
                                        }

                                        onActivated: {
                                            let selectedValue = model[currentIndex].value
                                            kcm.setQuickLayoutSlot(slotNumber, selectedValue)
                                        }
                                    }

                                    ToolButton {
                                        icon.name: "edit-clear"
                                        onClicked: {
                                            kcm.setQuickLayoutSlot(index + 1, "")
                                            slotLayoutCombo.currentIndex = 0
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: i18n("Clear shortcut")
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // TAB 4: APPEARANCE
        // ═══════════════════════════════════════════════════════════════════
        ScrollView {
            visible: stackLayout.currentIndex === 3
            clip: true
            contentWidth: availableWidth

            Kirigami.FormLayout {
                width: parent.width

                // Colors section
                Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Colors")
            }

            CheckBox {
                id: useSystemColorsCheck
                Kirigami.FormData.label: i18n("Color scheme:")
                text: i18n("Use system accent color")
                checked: kcm.useSystemColors
                onToggled: kcm.useSystemColors = checked
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Highlight:")
                visible: !useSystemColorsCheck.checked
                spacing: Kirigami.Units.smallSpacing

                ColorButton {
                    color: kcm.highlightColor
                    onClicked: {
                        highlightColorDialog.selectedColor = kcm.highlightColor
                        highlightColorDialog.open()
                    }
                }

                Label {
                    text: kcm.highlightColor.toString().toUpperCase()
                    font: Kirigami.Theme.fixedWidthFont
                }
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Inactive:")
                visible: !useSystemColorsCheck.checked
                spacing: Kirigami.Units.smallSpacing

                ColorButton {
                    color: kcm.inactiveColor
                    onClicked: {
                        inactiveColorDialog.selectedColor = kcm.inactiveColor
                        inactiveColorDialog.open()
                    }
                }

                Label {
                    text: kcm.inactiveColor.toString().toUpperCase()
                    font: Kirigami.Theme.fixedWidthFont
                }
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Border:")
                visible: !useSystemColorsCheck.checked
                spacing: Kirigami.Units.smallSpacing

                ColorButton {
                    color: kcm.borderColor
                    onClicked: {
                        borderColorDialog.selectedColor = kcm.borderColor
                        borderColorDialog.open()
                    }
                }

                Label {
                    text: kcm.borderColor.toString().toUpperCase()
                    font: Kirigami.Theme.fixedWidthFont
                }
            }

            // Pywal integration
            RowLayout {
                Kirigami.FormData.label: i18n("Import colors:")
                visible: !useSystemColorsCheck.checked
                spacing: Kirigami.Units.smallSpacing

                Button {
                    text: i18n("From pywal")
                    icon.name: "color-management"
                    onClicked: kcm.loadColorsFromPywal()
                }

                Button {
                    text: i18n("From file...")
                    icon.name: "document-open"
                    onClicked: colorFileDialog.open()
                }
            }

            // Opacity section
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Opacity")
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Active zone:")
                spacing: Kirigami.Units.smallSpacing

                Slider {
                    id: activeOpacitySlider
                    Layout.preferredWidth: constants.sliderPreferredWidth
                    from: 0
                    to: constants.opacitySliderMax
                    value: kcm.activeOpacity * constants.opacitySliderMax
                    onMoved: kcm.activeOpacity = value / constants.opacitySliderMax
                }

                Label {
                    text: Math.round(activeOpacitySlider.value) + "%"
                    Layout.preferredWidth: constants.sliderValueLabelWidth
                }
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Inactive zone:")
                spacing: Kirigami.Units.smallSpacing

                Slider {
                    id: inactiveOpacitySlider
                    Layout.preferredWidth: constants.sliderPreferredWidth
                    from: 0
                    to: constants.opacitySliderMax
                    value: kcm.inactiveOpacity * constants.opacitySliderMax
                    onMoved: kcm.inactiveOpacity = value / constants.opacitySliderMax
                }

                Label {
                    text: Math.round(inactiveOpacitySlider.value) + "%"
                    Layout.preferredWidth: constants.sliderValueLabelWidth
                }
            }

            // Border section
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Border")
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Width:")
                spacing: Kirigami.Units.smallSpacing

                SpinBox {
                    from: 0
                    to: constants.borderWidthMax
                    value: kcm.borderWidth
                    onValueModified: kcm.borderWidth = value
                }

                Label {
                    text: i18n("px")
                }
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Radius:")
                spacing: Kirigami.Units.smallSpacing

                SpinBox {
                    from: 0
                    to: constants.borderRadiusMax
                    value: kcm.borderRadius
                    onValueModified: kcm.borderRadius = value
                }

                Label {
                    text: i18n("px")
                }
            }

            // Effects section
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Effects")
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Blur:")
                text: i18n("Enable blur behind zones")
                checked: kcm.enableBlur
                onToggled: kcm.enableBlur = checked
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Shaders:")
                text: i18n("Enable shader effects")
                checked: kcm.enableShaderEffects
                onToggled: kcm.enableShaderEffects = checked
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Shader FPS:")
                enabled: kcm.enableShaderEffects
                spacing: Kirigami.Units.smallSpacing

                Slider {
                    id: shaderFpsSlider
                    Layout.preferredWidth: constants.sliderPreferredWidth
                    from: 30
                    to: 144
                    stepSize: 1
                    value: kcm.shaderFrameRate
                    onMoved: kcm.shaderFrameRate = Math.round(value)
                }

                Label {
                    text: Math.round(shaderFpsSlider.value) + " fps"
                    Layout.preferredWidth: constants.sliderValueLabelWidth + 10
                }
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Numbers:")
                text: i18n("Show zone numbers")
                checked: kcm.showZoneNumbers
                onToggled: kcm.showZoneNumbers = checked
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Animation:")
                text: i18n("Flash zones when switching layouts")
                checked: kcm.flashZonesOnSwitch
                onToggled: kcm.flashZonesOnSwitch = checked
            }

            CheckBox {
                id: showOsdCheckbox
                Kirigami.FormData.label: i18n("Notifications:")
                text: i18n("Show OSD when switching layouts")
                checked: kcm.showOsdOnLayoutSwitch
                onToggled: kcm.showOsdOnLayoutSwitch = checked
            }

            ComboBox {
                Kirigami.FormData.label: i18n("OSD style:")
                enabled: showOsdCheckbox.checked
                // OsdStyle enum: 0=None, 1=Text, 2=Preview
                // ComboBox only shows Text (1) and Preview (2), so map: 1->0, 2->1
                // Use Math.max to handle edge case where osdStyle could be 0 (None)
                currentIndex: Math.max(0, kcm.osdStyle - 1)
                model: [
                    i18n("Text only"),
                    i18n("Visual preview")
                ]
                onActivated: (index) => {
                    kcm.osdStyle = index + 1 // Convert back: 0->1 (Text), 1->2 (Preview)
                }
            }
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // TAB 5: BEHAVIOR
        // ═══════════════════════════════════════════════════════════════════
        ScrollView {
            visible: stackLayout.currentIndex === 4
            clip: true
            contentWidth: availableWidth

            Kirigami.FormLayout {
                width: parent.width

                // Activation section
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Activation")
                }

            ModifierCheckBoxes {
                id: dragActivationModifiers
                Kirigami.FormData.label: i18n("Zone activation modifiers:")
                modifierValue: kcm.dragActivationModifier
                tooltipEnabled: stackLayout.currentIndex === 4
                onValueModified: (value) => {
                    kcm.dragActivationModifier = value
                }
            }

            // Multi-zone selection
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Multi-Zone Selection")
            }

            ModifierCheckBoxes {
                id: multiZoneModifiers
                Kirigami.FormData.label: i18n("Multi-zone modifier:")
                modifierValue: kcm.multiZoneModifier
                tooltipEnabled: stackLayout.currentIndex === 4
                ToolTip.text: i18n("Hold this modifier combination while dragging to span windows across multiple zones")
                onValueModified: (value) => {
                    kcm.multiZoneModifier = value
                }
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Middle click:")
                text: i18n("Use middle mouse button to select multiple zones")
                checked: kcm.middleClickMultiZone
                onToggled: kcm.middleClickMultiZone = checked
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Edge threshold:")
                spacing: Kirigami.Units.smallSpacing

                SpinBox {
                    id: adjacentThresholdSpinBox
                    from: 0
                    to: constants.thresholdMax
                    value: kcm.adjacentThreshold
                    onValueModified: kcm.adjacentThreshold = value
                }

                Label {
                    text: i18n("px")
                }

                ToolTip.visible: adjacentThresholdSpinBox.hovered && stackLayout.currentIndex === 4
                ToolTip.text: i18n("Distance from zone edge for multi-zone selection")
            }

            // Zone settings
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Zone Settings")
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Zone padding:")
                spacing: Kirigami.Units.smallSpacing

                SpinBox {
                    from: 0
                    to: constants.thresholdMax
                    value: kcm.zonePadding
                    onValueModified: kcm.zonePadding = value
                }

                Label {
                    text: i18n("px")
                }
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Edge gap:")
                spacing: Kirigami.Units.smallSpacing

                SpinBox {
                    from: 0
                    to: constants.thresholdMax
                    value: kcm.outerGap
                    onValueModified: kcm.outerGap = value
                }

                Label {
                    text: i18n("px")
                }
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Display:")
                text: i18n("Show zones on all monitors while dragging")
                checked: kcm.showZonesOnAllMonitors
                onToggled: kcm.showZonesOnAllMonitors = checked
            }

            // Window behavior
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Window Behavior")
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Resolution:")
                text: i18n("Keep windows in zones when resolution changes")
                checked: kcm.keepWindowsInZonesOnResolutionChange
                onToggled: kcm.keepWindowsInZonesOnResolutionChange = checked
            }

            CheckBox {
                Kirigami.FormData.label: i18n("New windows:")
                text: i18n("Move new windows to their last used zone")
                checked: kcm.moveNewWindowsToLastZone
                onToggled: kcm.moveNewWindowsToLastZone = checked
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Unsnapping:")
                text: i18n("Restore original window size when unsnapping")
                checked: kcm.restoreOriginalSizeOnUnsnap
                onToggled: kcm.restoreOriginalSizeOnUnsnap = checked
            }

            ComboBox {
                id: stickyHandlingCombo
                Kirigami.FormData.label: i18n("Sticky windows:")
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: i18n("Treat as normal"), value: 0 },
                    { text: i18n("Restore only"), value: 1 },
                    { text: i18n("Ignore all"), value: 2 }
                ]
                currentIndex: indexForValue(kcm.stickyWindowHandling)
                onActivated: kcm.stickyWindowHandling = currentValue

                function indexForValue(value) {
                    for (let i = 0; i < model.length; i++) {
                        if (model[i].value === value) return i
                    }
                    return 0
                }

                ToolTip.visible: hovered && stackLayout.currentIndex === 4
                ToolTip.text: i18n("Sticky windows appear on all desktops. Choose how snapping should behave.")
            }
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // TAB 6: ZONE SELECTOR
        // ═══════════════════════════════════════════════════════════════════
        ScrollView {
            visible: stackLayout.currentIndex === 5
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

                // Position card - just the position picker
                Kirigami.Card {
                    Layout.fillWidth: true
                    enabled: kcm.zoneSelectorEnabled

                    header: Kirigami.Heading {
                        level: 3
                        text: i18n("Position")
                        padding: Kirigami.Units.smallSpacing
                    }

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.gridUnit * 2

                        PositionPicker {
                            id: positionPicker
                            position: kcm.zoneSelectorPosition
                            enabled: kcm.zoneSelectorEnabled
                            onPositionSelected: function(newPosition) {
                                kcm.zoneSelectorPosition = newPosition
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: i18n("Choose where the popup appears on screen. Edges and corners are supported.")
                            wrapMode: Text.WordWrap
                            opacity: 0.7
                        }
                    }
                }

                // Trigger & Behavior card
                Kirigami.Card {
                    Layout.fillWidth: true
                    enabled: kcm.zoneSelectorEnabled

                    header: Kirigami.Heading {
                        level: 3
                        text: i18n("Trigger & Layout")
                        padding: Kirigami.Units.smallSpacing
                    }

                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.largeSpacing

                        // Trigger distance
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Label {
                                text: i18n("Trigger distance")
                                font.bold: true
                            }

                            Label {
                                text: i18n("How close to the edge before the popup appears")
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
                                    to: constants.zoneSelectorTriggerMax
                                    value: kcm.zoneSelectorTriggerDistance
                                    stepSize: 10
                                    onMoved: kcm.zoneSelectorTriggerDistance = value
                                }

                                Label {
                                    text: kcm.zoneSelectorTriggerDistance + " px"
                                    Layout.preferredWidth: 55
                                    horizontalAlignment: Text.AlignRight
                                    font.family: "monospace"
                                }
                            }
                        }

                        Kirigami.Separator {
                            Layout.fillWidth: true
                        }

                        // Layout arrangement
                        Kirigami.FormLayout {
                            Layout.fillWidth: true

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
                                to: constants.zoneSelectorGridColumnsMax
                                value: kcm.zoneSelectorGridColumns
                                visible: kcm.zoneSelectorLayoutMode === 0
                                onValueModified: kcm.zoneSelectorGridColumns = value
                            }

                            SpinBox {
                                Kirigami.FormData.label: i18n("Max visible rows:")
                                from: 1
                                to: 10
                                value: kcm.zoneSelectorMaxRows
                                visible: kcm.zoneSelectorLayoutMode !== 1  // Hide for horizontal mode
                                onValueModified: kcm.zoneSelectorMaxRows = value

                                ToolTip.visible: hovered
                                ToolTip.delay: 500
                                ToolTip.text: i18n("Scrolling enabled when more rows exist")
                            }
                        }
                    }
                }

                // Preview Size card - simplified
                Kirigami.Card {
                    Layout.fillWidth: true
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
                                    font.family: "monospace"
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
                                ToolTip.delay: 500
                                ToolTip.text: i18n("~10%% of screen width (120-280px)")
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
                                ToolTip.delay: 500
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
                                ToolTip.delay: 500
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
                                ToolTip.delay: 500
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
                                ToolTip.delay: 500
                                ToolTip.text: i18n("Custom size with slider")
                            }
                        }

                        // Custom size slider - only visible when Custom is selected
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            Layout.maximumWidth: 400
                            visible: sizeButtonRow.selectedSize === 4
                            spacing: Kirigami.Units.smallSpacing

                            Label {
                                text: i18n("Size:")
                            }

                            Slider {
                                id: customSizeSlider
                                Layout.fillWidth: true
                                from: constants.zoneSelectorPreviewWidthMin
                                to: constants.zoneSelectorPreviewWidthMax
                                value: kcm.zoneSelectorPreviewWidth
                                stepSize: 10
                                onMoved: {
                                    kcm.zoneSelectorPreviewWidth = value
                                    // Always maintain aspect ratio
                                    var newHeight = Math.round(value / root.screenAspectRatio)
                                    newHeight = Math.max(constants.zoneSelectorPreviewHeightMin, Math.min(constants.zoneSelectorPreviewHeightMax, newHeight))
                                    kcm.zoneSelectorPreviewHeight = newHeight
                                }
                            }

                            Label {
                                text: kcm.zoneSelectorPreviewWidth + " px"
                                Layout.preferredWidth: 55
                                horizontalAlignment: Text.AlignRight
                                font.family: "monospace"
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
        }

        // ═══════════════════════════════════════════════════════════════════
        // TAB 7: EXCLUSIONS
        // ═══════════════════════════════════════════════════════════════════
        ScrollView {
            visible: stackLayout.currentIndex === 6
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: Kirigami.Units.largeSpacing

                Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Information
                text: i18n("Windows from excluded applications or with excluded window classes will not snap to zones.")
                visible: true
            }

            // Window type and size exclusions
            Kirigami.Card {
                Layout.fillWidth: true

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Window Filtering")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing
                    Layout.margins: Kirigami.Units.smallSpacing

                    CheckBox {
                        text: i18n("Exclude transient windows (dialogs, utilities, tooltips)")
                        checked: kcm.excludeTransientWindows
                        onToggled: kcm.excludeTransientWindows = checked
                    }

                    Label {
                        text: i18n("Minimum window size for snapping:")
                        Layout.topMargin: Kirigami.Units.smallSpacing
                    }

                    RowLayout {
                        spacing: Kirigami.Units.largeSpacing

                        RowLayout {
                            Label {
                                text: i18n("Width:")
                            }
                            SpinBox {
                                from: 0
                                to: 1000
                                stepSize: 10
                                value: kcm.minimumWindowWidth
                                onValueModified: kcm.minimumWindowWidth = value

                                textFromValue: function(value) {
                                    return value === 0 ? i18n("Disabled") : value + " px"
                                }
                            }
                        }

                        RowLayout {
                            Label {
                                text: i18n("Height:")
                            }
                            SpinBox {
                                from: 0
                                to: 1000
                                stepSize: 10
                                value: kcm.minimumWindowHeight
                                onValueModified: kcm.minimumWindowHeight = value

                                textFromValue: function(value) {
                                    return value === 0 ? i18n("Disabled") : value + " px"
                                }
                            }
                        }
                    }

                    Label {
                        text: i18n("Windows smaller than these dimensions will not snap to zones. Set to 0 to disable.")
                        font.italic: true
                        opacity: 0.7
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            // Excluded applications
            Kirigami.Card {
                Layout.fillWidth: true
                Layout.fillHeight: true

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Excluded Applications")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.smallSpacing

                        TextField {
                            id: excludedAppField
                            Layout.fillWidth: true
                            placeholderText: i18n("Application name (e.g., firefox, konsole)")

                            onAccepted: {
                                if (text.length > 0) {
                                    kcm.addExcludedApp(text)
                                    text = ""
                                }
                            }
                        }

                        Button {
                            text: i18n("Add")
                            icon.name: "list-add"
                            enabled: excludedAppField.text.length > 0
                            onClicked: {
                                kcm.addExcludedApp(excludedAppField.text)
                                excludedAppField.text = ""
                            }
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: Kirigami.Units.smallSpacing
                        clip: true
                        model: kcm.excludedApplications

                        delegate: ItemDelegate {
                            width: ListView.view.width
                            required property string modelData
                            required property int index

                            contentItem: RowLayout {
                                Kirigami.Icon {
                                    source: "application-x-executable"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                Label {
                                    text: modelData
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                ToolButton {
                                    icon.name: "edit-delete"
                                    onClicked: kcm.removeExcludedApp(index)
                                }
                            }
                        }

                        Kirigami.PlaceholderMessage {
                            anchors.centerIn: parent
                            width: parent.width - Kirigami.Units.gridUnit * 4
                            visible: parent.count === 0
                            text: i18n("No excluded applications")
                            explanation: i18n("Add application names above to exclude them from zone snapping")
                        }
                    }
                }
            }

            // Excluded window classes
            Kirigami.Card {
                Layout.fillWidth: true
                Layout.fillHeight: true

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Excluded Window Classes")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.smallSpacing

                        TextField {
                            id: excludedClassField
                            Layout.fillWidth: true
                            placeholderText: i18n("Window class (use xprop to find)")

                            onAccepted: {
                                if (text.length > 0) {
                                    kcm.addExcludedWindowClass(text)
                                    text = ""
                                }
                            }
                        }

                        Button {
                            text: i18n("Add")
                            icon.name: "list-add"
                            enabled: excludedClassField.text.length > 0
                            onClicked: {
                                kcm.addExcludedWindowClass(excludedClassField.text)
                                excludedClassField.text = ""
                            }
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: Kirigami.Units.smallSpacing
                        clip: true
                        model: kcm.excludedWindowClasses

                        delegate: ItemDelegate {
                            width: ListView.view.width
                            required property string modelData
                            required property int index

                            contentItem: RowLayout {
                                Kirigami.Icon {
                                    source: "window"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                Label {
                                    text: modelData
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    font: Kirigami.Theme.fixedWidthFont
                                }

                                ToolButton {
                                    icon.name: "edit-delete"
                                    onClicked: kcm.removeExcludedWindowClass(index)
                                }
                            }
                        }

                        Kirigami.PlaceholderMessage {
                            anchors.centerIn: parent
                            width: parent.width - Kirigami.Units.gridUnit * 4
                            visible: parent.count === 0
                            text: i18n("No excluded window classes")
                            explanation: i18n("Use 'xprop | grep WM_CLASS' to find window classes")
                        }
                    }
                }
            }
            }
        }

    }

    // ═══════════════════════════════════════════════════════════════════════
    // PROPERTY SYNC CONNECTIONS
    // ═══════════════════════════════════════════════════════════════════════

    // ═══════════════════════════════════════════════════════════════════════
    // DIALOGS
    // ═══════════════════════════════════════════════════════════════════════

    // Color dialogs
    ColorDialog {
        id: highlightColorDialog
        title: i18n("Choose Highlight Color")
        onAccepted: kcm.highlightColor = selectedColor
    }

    ColorDialog {
        id: inactiveColorDialog
        title: i18n("Choose Inactive Zone Color")
        onAccepted: kcm.inactiveColor = selectedColor
    }

    ColorDialog {
        id: borderColorDialog
        title: i18n("Choose Border Color")
        onAccepted: kcm.borderColor = selectedColor
    }

    // File dialogs
    FileDialog {
        id: importDialog
        title: i18n("Import Layout")
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: kcm.importLayout(selectedFile.toString().replace("file://", ""))
    }

    FileDialog {
        id: exportDialog
        title: i18n("Export Layout")
        nameFilters: ["JSON files (*.json)"]
        fileMode: FileDialog.SaveFile
        property string layoutId: ""
        onAccepted: kcm.exportLayout(layoutId, selectedFile.toString().replace("file://", ""))
    }

    FileDialog {
        id: colorFileDialog
        title: i18n("Import Colors from File")
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: kcm.loadColorsFromFile(selectedFile.toString().replace("file://", ""))
    }

    // Delete confirmation dialog
    Kirigami.PromptDialog {
        id: deleteConfirmDialog
        title: i18n("Delete Layout")
        subtitle: i18n("Are you sure you want to delete '%1'?", layoutToDelete?.name ?? "")
        standardButtons: Kirigami.Dialog.Yes | Kirigami.Dialog.No
        preferredWidth: Math.min(Kirigami.Units.gridUnit * 30, parent.width * 0.8)
        property var layoutToDelete: null

        onAccepted: {
            if (layoutToDelete) {
                kcm.deleteLayout(layoutToDelete.id)
                layoutToDelete = null
            }
        }
        onRejected: layoutToDelete = null
    }

    // Layout editor launcher dialog
    Kirigami.Dialog {
        id: layoutEditorSheet
        title: i18n("Edit Layout")
        standardButtons: Kirigami.Dialog.Close
        preferredWidth: Math.min(Kirigami.Units.gridUnit * 25, parent.width * 0.8)

        property string layoutId: ""
        property string layoutName: ""

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            width: parent.width

            Label {
                Layout.fillWidth: true
                text: i18n("Use the visual editor to create and modify zone layouts.")
                wrapMode: Text.WordWrap
            }

            Button {
                Layout.fillWidth: true
                text: i18n("Open Layout Editor")
                icon.name: "document-edit"
                onClicked: {
                    kcm.openEditor()
                    layoutEditorSheet.close()
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            Label {
                Layout.fillWidth: true
                text: i18n("You can also edit layout JSON files directly:")
                wrapMode: Text.WordWrap
                opacity: 0.7
            }

            Button {
                text: i18n("Open Layouts Folder")
                icon.name: "folder-open"
                onClicked: {
                    // Use KIO to open folder (better integration with KDE)
                    Qt.openUrlExternally("file://" + StandardPaths.writableLocation(StandardPaths.StandardLocation.GenericDataLocation) + "/plasmazones/layouts")
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // HELPER COMPONENTS
    // ═══════════════════════════════════════════════════════════════════════

    component ColorButton: Rectangle {
        id: colorBtn
        width: constants.colorButtonSize
        height: constants.colorButtonSize
        radius: Kirigami.Units.smallSpacing  // Use theme spacing
        border.color: Kirigami.Theme.disabledTextColor
        border.width: 1

        signal clicked()

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: colorBtn.clicked()
        }

        // Checkerboard pattern for transparency preview
        Canvas {
            anchors.fill: parent
            anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
            visible: colorBtn.color.a < 1.0

            onPaint: {
                var ctx = getContext("2d")
                var size = 4
                // Use theme-neutral colors for checkerboard pattern (standard for transparency preview)
                var lightGray = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.2)
                var white = Qt.rgba(1.0, 1.0, 1.0, 1.0)
                for (var x = 0; x < width; x += size) {
                    for (var y = 0; y < height; y += size) {
                        ctx.fillStyle = ((x / size + y / size) % 2 === 0) ? lightGray : white
                        ctx.fillRect(x, y, size, size)
                    }
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
            radius: Math.max(0, parent.radius - Math.round(Kirigami.Units.devicePixelRatio))
            color: colorBtn.color
        }
    }
}
