// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

/// Main editor window for zone layout editing
Window {
    // ═══════════════════════════════════════════════════════════════════
    // DIALOGS
    // ═══════════════════════════════════════════════════════════════════
    // ═══════════════════════════════════════════════════════════════════
    // CONNECTIONS
    // ═══════════════════════════════════════════════════════════════════

    id: editorWindow

    // Context properties (set in main.cpp)
    property var _editorController: editorController
    property var _availableScreens: availableScreens
    // State properties
    property string selectedZoneId: editorWindow._editorController ? (editorWindow._editorController.selectedZoneId || "") : ""
    property var selectedZoneIds: editorWindow._editorController ? editorWindow._editorController.selectedZoneIds : []
    property int selectionCount: editorWindow._editorController ? editorWindow._editorController.selectionCount : 0
    property bool hasMultipleSelection: editorWindow._editorController ? editorWindow._editorController.hasMultipleSelection : false
    property string selectionAnchorId: "" // For Shift+click range selection
    property bool isDrawingZone: false
    property point drawStart: Qt.point(0, 0)
    property rect drawRect: Qt.rect(0, 0, 0, 0)
    // Fullscreen editing mode - hides all panels for distraction-free editing
    property bool fullscreenMode: false
    // Zone spacing matches zone padding from settings
    readonly property real zoneSpacing: editorWindow._editorController ? editorWindow._editorController.zonePadding : Kirigami.Units.gridUnit
    property var _zonesRepeater: null
    // Helper to get selected zone data - reactive to both selectedZoneId AND zones changes
    // Force re-evaluation by accessing zones in the binding
    // Use Qt.callLater to defer evaluation and avoid invalid context errors during zone removal
    property var selectedZone: {
        var controller = editorWindow._editorController;
        if (!controller)
            return null;

        try {
            var zonesList = controller.zones;
            var zoneId = selectedZoneId;
            if (!zoneId || zoneId === "")
                return null;

            if (!zonesList || !zonesList.length)
                return null;

            for (var i = 0; i < zonesList.length; i++) {
                var zone = zonesList[i];
                if (zone && zone.id === zoneId)
                    return zone;

            }
        } catch (e) {
            console.warn("EditorWindow: Error accessing selectedZone:", e);
            return null;
        }
        return null;
    }

    function toggleFullscreenMode() {
        fullscreenMode = !fullscreenMode;
    }

    // Helper to check if a zone is currently selected
    function isZoneSelected(zoneId) {
        if (!zoneId || !selectedZoneIds)
            return false;

        for (var i = 0; i < selectedZoneIds.length; i++) {
            if (selectedZoneIds[i] === zoneId)
                return true;

        }
        return false;
    }

    // Handle zone click with modifier keys for multi-selection
    function handleZoneClick(zoneId, modifiers) {
        if (!editorWindow._editorController || !zoneId)
            return ;

        if (modifiers & Qt.ControlModifier) {
            // Ctrl+click: toggle zone in/out of selection
            editorWindow._editorController.toggleSelection(zoneId);
            if (editorWindow._editorController.isSelected(zoneId))
                editorWindow.selectionAnchorId = zoneId;

        } else if ((modifiers & Qt.ShiftModifier) && editorWindow.selectionAnchorId !== "") {
            // Shift+click: range selection from anchor to clicked zone
            editorWindow._editorController.selectRange(editorWindow.selectionAnchorId, zoneId);
        } else {
            // Normal click: single selection (clears others)
            editorWindow._editorController.setSelectedZoneIds([zoneId]);
            editorWindow.selectionAnchorId = zoneId;
            // Also update local for backward compatibility
            editorWindow.selectedZoneId = zoneId;
        }
    }

    function findZoneById(zoneId) {
        if (!zoneId)
            return null;

        var controller = editorWindow._editorController;
        if (!controller || !controller.zones)
            return null;

        var zones = controller.zones;
        for (var i = 0; i < zones.length; i++) {
            if (zones[i].id === zoneId)
                return zones[i];

        }
        return null;
    }

    // Helper function to find zone item by ID from the Repeater
    function findZoneItemById(zoneId) {
        if (!zoneId || !_zonesRepeater)
            return null;

        for (var i = 0; i < _zonesRepeater.count; i++) {
            var item = _zonesRepeater.itemAt(i);
            if (item && item.zoneId === zoneId)
                return item;

        }
        return null;
    }

    // Helper function to format shortcut for display (delegated to EditorShortcuts)
    function formatShortcut(shortcut) {
        return editorShortcuts.formatShortcut(shortcut);
    }

    // Window flags - proper for both X11 and Wayland
    // On Wayland, WindowStaysOnTopHint doesn't work - use LayerShellQt if available
    flags: Qt.FramelessWindowHint | (Qt.platform.os === "wayland" ? 0 : Qt.WindowStaysOnTopHint) | Qt.WindowFullScreenButtonHint
    color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.7)
    visibility: Window.FullScreen
    title: i18nc("@title", "Layout Editor")
    Component.onCompleted: {
        // Initialize selectedZoneId from editorController context property
        if (editorWindow._editorController) {
            editorWindow.selectedZoneId = editorWindow._editorController.selectedZoneId || "";
            // Set default zone colors from theme (so new zones use theme colors)
            // Match PropertyPanel.qml multiselect/single-select fallback defaults
            var highlightColor = Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5);
            var inactiveColor = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.25);
            var borderColor = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.8);
            // Convert QML colors to ARGB hex strings
            var highlightHex = "#" + Math.round(highlightColor.a * 255).toString(16).padStart(2, '0').toUpperCase() + Math.round(highlightColor.r * 255).toString(16).padStart(2, '0').toUpperCase() + Math.round(highlightColor.g * 255).toString(16).padStart(2, '0').toUpperCase() + Math.round(highlightColor.b * 255).toString(16).padStart(2, '0').toUpperCase();
            var inactiveHex = "#" + Math.round(inactiveColor.a * 255).toString(16).padStart(2, '0').toUpperCase() + Math.round(inactiveColor.r * 255).toString(16).padStart(2, '0').toUpperCase() + Math.round(inactiveColor.g * 255).toString(16).padStart(2, '0').toUpperCase() + Math.round(inactiveColor.b * 255).toString(16).padStart(2, '0').toUpperCase();
            var borderHex = "#" + Math.round(borderColor.a * 255).toString(16).padStart(2, '0').toUpperCase() + Math.round(borderColor.r * 255).toString(16).padStart(2, '0').toUpperCase() + Math.round(borderColor.g * 255).toString(16).padStart(2, '0').toUpperCase() + Math.round(borderColor.b * 255).toString(16).padStart(2, '0').toUpperCase();
            editorWindow._editorController.setDefaultZoneColors(highlightHex, inactiveHex, borderHex);
            // If no layout loaded, create new
            if (editorWindow._editorController.layoutId === "")
                editorWindow._editorController.createNewLayout();

        }
        // Request focus on drawingArea for keyboard navigation
        // Use a timer to ensure focus is set after window is fully shown
        Qt.callLater(function() {
            drawingArea.forceActiveFocus();
        });
    }
    // Ensure drawingArea has focus when window becomes visible
    onVisibleChanged: {
        if (visible)
            Qt.callLater(function() {
            drawingArea.forceActiveFocus();
        });

    }

    ZoneOperations {
        id: zoneOps
    }
    // Helper function to find zone by ID

    // ═══════════════════════════════════════════════════════════════════
    // SHORTCUTS - Extracted to EditorShortcuts.qml
    // ═══════════════════════════════════════════════════════════════════
    EditorShortcuts {
        id: editorShortcuts

        editorController: editorWindow._editorController
        editorWindow: editorWindow
        confirmCloseDialog: confirmCloseDialog
        helpDialog: helpDialog
        fullscreenMode: editorWindow.fullscreenMode
        onFullscreenToggled: editorWindow.toggleFullscreenMode()
    }

    // Keyboard navigation handler
    KeyboardNavigation {
        id: keyboardNav

        editorController: editorWindow._editorController
        drawingArea: drawingArea
    }

    // ═══════════════════════════════════════════════════════════════════
    // TOP BAR
    // ═══════════════════════════════════════════════════════════════════
    TopBar {
        id: topBar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: !editorWindow.fullscreenMode
        // Pass stored context properties to avoid scoping issues
        editorController: editorWindow._editorController
        availableScreens: editorWindow._availableScreens
        confirmCloseDialog: confirmCloseDialog
        helpDialog: helpDialog
        shaderDialog: shaderDialog
        importDialog: importDialog
        editorWindow: editorWindow
        exportDialog: exportDialog
        fullscreenMode: editorWindow.fullscreenMode
        onFullscreenToggled: editorWindow.toggleFullscreenMode()

        Behavior on opacity {
            NumberAnimation {
                duration: 200
                easing.type: Easing.OutCubic
            }

        }

    }

    // ═══════════════════════════════════════════════════════════════════
    // MAIN LAYOUT - Stable layout system
    // ═══════════════════════════════════════════════════════════════════
    RowLayout {
        id: mainLayout

        anchors.top: editorWindow.fullscreenMode ? parent.top : topBar.bottom
        anchors.bottom: editorWindow.fullscreenMode ? parent.bottom : bottomBar.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 0
        spacing: 0

        // ═══════════════════════════════════════════════════════════════
        // CANVAS AREA - Stable width, doesn't change with panel
        // ═══════════════════════════════════════════════════════════════
        Item {
            id: canvasArea

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width - (propertiesPanel.visible ? propertiesPanel.width : 0)
            Layout.minimumWidth: 400

            // Actual drawing area (with margins matching zone padding from settings)
            Item {
                // Allow Tab/Shift+Tab for standard focus navigation (accessibility requirement)

                id: drawingArea

                objectName: "drawingArea" // Required for focus restoration from child components
                anchors.fill: parent
                anchors.margins: editorWindow._editorController ? editorWindow._editorController.zonePadding : Kirigami.Units.gridUnit
                focus: true // Enable keyboard focus for navigation
                // Keyboard navigation - uses extracted KeyboardNavigation component
                Keys.priority: Keys.AfterItem
                // Allow standard Tab navigation first
                Keys.enabled: true
                Keys.onPressed: function(event) {
                    keyboardNav.handleKeyPress(event);
                }

                // ═══════════════════════════════════════════════════════════
                // GRID OVERLAY - Extracted to GridOverlay.qml
                // ═══════════════════════════════════════════════════════════
                GridOverlay {
                    id: gridCanvas

                    editorController: editorWindow._editorController
                }

                // Zones - use zone ID for stable selection
                // Zones are children of drawingArea, positioned relative to it
                Repeater {
                    id: zonesRepeater

                    model: editorWindow._editorController ? editorWindow._editorController.zones : []
                    Component.onCompleted: {
                        editorWindow._zonesRepeater = zonesRepeater;
                    }

                    EditorZone {
                        // Pass zone center coords to use smartFillZone (same algorithm as calculateFillRegion)

                        required property var modelData
                        required property int index

                        // Use full drawing area dimensions for calculations
                        canvasWidth: drawingArea.width
                        canvasHeight: drawingArea.height
                        // Bind directly to modelData - when zones list updates, modelData updates
                        zoneData: modelData
                        zoneId: modelData.id || ""
                        isSelected: editorWindow.isZoneSelected(modelData.id)
                        isPartOfMultiSelection: isSelected && editorWindow.hasMultipleSelection
                        controller: editorWindow._editorController // Pass controller for snapping
                        zoneSpacing: editorWindow.zoneSpacing // Pass spacing for gaps
                        snapIndicator: snapIndicator // Pass snapIndicator for visual feedback
                        // Z-order: base of 60 (above DividerManager z:50) + zOrder from model
                        z: 60 + (modelData.zOrder !== undefined ? modelData.zOrder : 0)
                        onClicked: function(event) {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow.handleZoneClick(modelData.id, event.modifiers);

                        }
                        onGeometryChanged: function(x, y, w, h) {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.updateZoneGeometry(modelData.id, x, y, w, h);

                        }
                        onDeleteRequested: {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.deleteZone(modelData.id);

                        }
                        onDuplicateRequested: {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.duplicateZone(modelData.id);

                        }
                        onSplitHorizontalRequested: {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.splitZone(modelData.id, true);

                        }
                        onSplitVerticalRequested: {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.splitZone(modelData.id, false);

                        }
                        onExpandToFillRequested: {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.expandToFillSpace(modelData.id);

                        }
                        onExpandToFillWithCoords: function(mouseX, mouseY) {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.expandToFillSpace(modelData.id, mouseX, mouseY);

                        }
                        onDeleteWithFillRequested: {
                            if (editorWindow._editorController && modelData && modelData.id)
                                zoneOps.deleteWithFillAnimation(modelData.id, editorWindow._editorController, editorWindow._zonesRepeater, drawingArea.width, drawingArea.height);

                        }
                        onBringToFrontRequested: {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.bringToFront(modelData.id);

                        }
                        onSendToBackRequested: {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.sendToBack(modelData.id);

                        }
                        onBringForwardRequested: {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.bringForward(modelData.id);

                        }
                        onSendBackwardRequested: {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.sendBackward(modelData.id);

                        }
                        // Track zone operations for snap/dimension indicators
                        onOperationStarted: function(zoneId, x, y, width, height) {
                            activeZoneOperation.active = true;
                            activeZoneOperation.x = x;
                            activeZoneOperation.y = y;
                            activeZoneOperation.width = width;
                            activeZoneOperation.height = height;
                        }
                        onOperationUpdated: function(zoneId, x, y, width, height) {
                            activeZoneOperation.x = x;
                            activeZoneOperation.y = y;
                            activeZoneOperation.width = width;
                            activeZoneOperation.height = height;
                        }
                        onOperationEnded: function(zoneId) {
                            activeZoneOperation.active = false;
                            // Clear snap lines when operation ends
                            if (snapIndicator)
                                snapIndicator.clearSnapLines();

                        }
                    }

                }

                // Dividers between zones - allow resizing multiple zones at once
                DividerManager {
                    id: dividerManager

                    editorController: editorWindow._editorController
                    zoneSpacing: editorWindow.zoneSpacing
                    drawingArea: drawingArea
                    zonesRepeater: zonesRepeater
                }

                // Snap line visualization
                SnapIndicator {
                    id: snapIndicator

                    // anchors.fill is set in SnapIndicator.qml to fill parent (drawingArea)
                    showSnapLines: true
                }

                // Dimension tooltip
                DimensionTooltip {
                    id: dimensionTooltip

                    zoneX: activeZoneOperation.x
                    zoneY: activeZoneOperation.y
                    zoneWidth: activeZoneOperation.width
                    zoneHeight: activeZoneOperation.height
                    canvasWidth: drawingArea.width
                    canvasHeight: drawingArea.height
                    showDimensions: activeZoneOperation.active
                }

                // Track active zone operation state
                QtObject {
                    id: activeZoneOperation

                    property bool active: false
                    property real x: 0
                    property real y: 0
                    property real width: 0
                    property real height: 0
                }

                // ═══════════════════════════════════════════════════════════
                // CANVAS MOUSE HANDLER - Extracted to CanvasMouseHandler.qml
                // ═══════════════════════════════════════════════════════════
                CanvasMouseHandler {
                    editorWindow: editorWindow
                    editorController: editorWindow._editorController
                    drawingArea: drawingArea
                }

            }

        }

        // ═══════════════════════════════════════════════════════════════
        // PROPERTIES PANEL (Right side) - Stable width, opacity animation
        // ═══════════════════════════════════════════════════════════════
        PropertyPanel {
            id: propertiesPanel

            visible: !editorWindow.fullscreenMode
            editorController: editorWindow._editorController
            selectedZoneId: editorWindow.selectedZoneId
            selectedZone: editorWindow.selectedZone
            selectionCount: editorWindow.selectionCount
            hasMultipleSelection: editorWindow.hasMultipleSelection
        }

    }

    // ═══════════════════════════════════════════════════════════════════
    // BOTTOM TOOLBAR
    // ═══════════════════════════════════════════════════════════════════
    ControlBar {
        id: bottomBar

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: !editorWindow.fullscreenMode
        editorController: editorWindow._editorController
        confirmCloseDialog: confirmCloseDialog
        editorWindow: editorWindow
    }

    // ═══════════════════════════════════════════════════════════════════
    // FULLSCREEN EXIT BUTTON - Floating button to exit fullscreen mode
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id: fullscreenExitButton

        visible: editorWindow.fullscreenMode
        width: exitButtonRow.width + Kirigami.Units.gridUnit * 2
        height: Kirigami.Units.gridUnit * 3
        radius: Kirigami.Units.smallSpacing
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.9)
        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
        border.width: 1
        z: 200
        // Fade in/out animation
        opacity: editorWindow.fullscreenMode ? 1 : 0

        anchors {
            top: parent.top
            right: parent.right
            margins: Kirigami.Units.largeSpacing
        }

        Row {
            id: exitButtonRow

            anchors.centerIn: parent
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: "view-restore"
                width: Kirigami.Units.iconSizes.small
                height: width
                anchors.verticalCenter: parent.verticalCenter
            }

            QQC.Label {
                text: i18n("Exit Fullscreen (F11)")
                anchors.verticalCenter: parent.verticalCenter
            }

        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            onClicked: editorWindow.toggleFullscreenMode()
            onEntered: parent.color = Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.3)
            onExited: parent.color = Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.9)
        }

        Behavior on opacity {
            NumberAnimation {
                duration: 200
            }

        }

    }

    Kirigami.PromptDialog {
        id: confirmCloseDialog

        title: i18nc("@title:window", "Unsaved Changes")
        subtitle: i18nc("@info", "You have unsaved changes. What would you like to do?")
        standardButtons: Kirigami.Dialog.Save | Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
        preferredWidth: Kirigami.Units.gridUnit * 24
        onAccepted: {
            if (editorWindow._editorController)
                editorWindow._editorController.saveLayout();

            editorWindow.close();
        }
        onDiscarded: {
            editorWindow.close();
        }
    }

    // File dialogs for import/export
    FileDialog {
        id: importDialog

        title: i18nc("@title:window", "Import Layout")
        nameFilters: [i18nc("@item:inlistbox", "JSON files (*.json)"), i18nc("@item:inlistbox", "All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (editorWindow._editorController) {
                var filePath = selectedFile.toString().replace("file://", "");
                editorWindow._editorController.importLayout(filePath);
            }
        }
    }

    FileDialog {
        id: exportDialog

        title: i18nc("@title:window", "Export Layout")
        nameFilters: [i18nc("@item:inlistbox", "JSON files (*.json)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        onAccepted: {
            if (editorWindow._editorController) {
                var filePath = selectedFile.toString().replace("file://", "");
                editorWindow._editorController.exportLayout(filePath);
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // NOTIFICATIONS - Extracted to EditorNotifications.qml
    // ═══════════════════════════════════════════════════════════════════
    EditorNotifications {
        id: notifications

        anchors.fill: parent
        anchorItem: topBar
        windowWidth: editorWindow.width
        z: 200
    }

    // ═══════════════════════════════════════════════════════════════════
    // HELP DIALOG - Content extracted to HelpDialogContent.qml
    // ═══════════════════════════════════════════════════════════════════
    Kirigami.Dialog {
        id: helpDialog

        title: i18nc("@title:window", "Layout Editor Help")
        standardButtons: Kirigami.Dialog.Close
        preferredWidth: Kirigami.Units.gridUnit * 32

        HelpDialogContent {
            editorController: editorWindow._editorController
            editorWindow: editorWindow
        }

    }

    // ═══════════════════════════════════════════════════════════════════
    // SHADER SETTINGS DIALOG
    // ═══════════════════════════════════════════════════════════════════
    Kirigami.Dialog {
        id: shaderDialog

        title: i18nc("@title:window", "Shader Effect")
        preferredWidth: Kirigami.Units.gridUnit * 32

        // Pending state - buffered until Apply is clicked
        property var pendingParams: ({})
        property string pendingShaderId: ""
        property bool pendingHasEffect: false

        // Initialize pending state when dialog opens
        onOpened: {
            shaderDialog.initializePendingState();
        }

        // When pending shader changes, reinitialize params with new shader's defaults
        onPendingShaderIdChanged: {
            if (shaderDialog.visible) {
                shaderDialog.initializePendingParamsForShader();
            }
        }

        function initializePendingState() {
            if (!editorWindow._editorController) return;
            pendingShaderId = editorWindow._editorController.currentShaderId || "";
            pendingHasEffect = editorWindow._editorController.hasShaderEffect;
            // Copy current params to pending
            var current = editorWindow._editorController.currentShaderParams || {};
            var copy = {};
            for (var key in current) {
                copy[key] = current[key];
            }
            pendingParams = copy;
        }

        function initializePendingParamsForShader() {
            // When shader changes, initialize params with defaults from new shader
            var info = shaderDialog.currentShaderInfo;
            if (!info || !info.parameters) {
                pendingParams = {};
                return;
            }
            var defaults = {};
            for (var i = 0; i < info.parameters.length; i++) {
                var param = info.parameters[i];
                if (param && param.id !== undefined && param.default !== undefined) {
                    defaults[param.id] = param.default;
                }
            }
            pendingParams = defaults;
        }

        function setPendingParam(paramId, value) {
            var copy = {};
            for (var key in pendingParams) {
                copy[key] = pendingParams[key];
            }
            copy[paramId] = value;
            pendingParams = copy;
        }

        function applyChanges() {
            if (!editorWindow._editorController) return;

            // Apply shader selection
            if (pendingShaderId !== editorWindow._editorController.currentShaderId) {
                editorWindow._editorController.currentShaderId = pendingShaderId;
            }

            // Apply all pending parameter changes
            var currentParams = editorWindow._editorController.currentShaderParams || {};
            for (var paramId in pendingParams) {
                if (pendingParams[paramId] !== currentParams[paramId]) {
                    editorWindow._editorController.setShaderParameter(paramId, pendingParams[paramId]);
                }
            }
        }

        function resetToDefaults() {
            if (!editorWindow._editorController) return;
            // Get default values from shader parameters
            var defaults = {};
            var params = shaderDialog.shaderParams;
            for (var i = 0; i < params.length; i++) {
                var param = params[i];
                if (param && param.id !== undefined && param.default !== undefined) {
                    defaults[param.id] = param.default;
                }
            }
            pendingParams = defaults;
        }

        standardButtons: Kirigami.Dialog.NoButton

        // Custom footer with Defaults on left, Apply on right (KCM style)
        footer: QQC.DialogButtonBox {
            QQC.Button {
                text: i18nc("@action:button", "Defaults")
                icon.name: "edit-undo"
                visible: shaderDialog.shaderParams.length > 0
                QQC.DialogButtonBox.buttonRole: QQC.DialogButtonBox.ResetRole
                onClicked: shaderDialog.resetToDefaults()
            }

            QQC.Button {
                text: i18nc("@action:button", "Apply")
                icon.name: "dialog-ok-apply"
                QQC.DialogButtonBox.buttonRole: QQC.DialogButtonBox.AcceptRole
                onClicked: {
                    shaderDialog.applyChanges();
                    shaderDialog.close();
                }
            }
        }

        // Helper to get current shader info from the available shaders list (uses pending state)
        property var currentShaderInfo: {
            if (!editorWindow._editorController) return null;
            var shaders = editorWindow._editorController.availableShaders;
            var targetId = shaderDialog.pendingShaderId;
            if (!shaders || !targetId) return null;
            for (var i = 0; i < shaders.length; i++) {
                if (shaders[i] && shaders[i].id === targetId) {
                    return shaders[i];
                }
            }
            return null;
        }

        property bool hasShaderEffect: shaderDialog.pendingShaderId !== "" && shaderDialog.pendingShaderId !== shaderDialog.noneShaderId
        property var shaderParams: shaderDialog.currentShaderInfo ? (shaderDialog.currentShaderInfo.parameters || []) : []
        property var currentParams: shaderDialog.pendingParams
        property string noneShaderId: editorWindow._editorController ? editorWindow._editorController.noneShaderUuid : ""

        function firstEffectId() {
            var shaders = editorWindow._editorController ? editorWindow._editorController.availableShaders : [];
            for (var i = 0; i < shaders.length; i++) {
                if (shaders[i] && shaders[i].id && shaders[i].id !== shaderDialog.noneShaderId) {
                    return shaders[i].id;
                }
            }
            return shaderDialog.noneShaderId;
        }

        function parameterValue(paramId, fallback) {
            var params = shaderDialog.pendingParams;
            if (params && params[paramId] !== undefined) {
                return params[paramId];
            }
            return fallback;
        }

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            Kirigami.FormLayout {
                Layout.fillWidth: true

                QQC.Switch {
                    Kirigami.FormData.label: i18nc("@label", "Enable effect:")
                    checked: shaderDialog.hasShaderEffect

                    Accessible.name: i18nc("@label", "Enable shader effect")
                    Accessible.description: i18nc("@info", "Turn the shader effect on or off for zone overlays")

                    onToggled: {
                        if (checked) {
                            if (shaderDialog.pendingShaderId === shaderDialog.noneShaderId) {
                                shaderDialog.pendingShaderId = shaderDialog.firstEffectId();
                            }
                        } else {
                            shaderDialog.pendingShaderId = shaderDialog.noneShaderId;
                        }
                    }
                }
            }

            // Message when effect is disabled
            QQC.Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
                visible: !shaderDialog.hasShaderEffect
                text: i18nc("@info", "The shader effect is disabled for this layout.")
                wrapMode: Text.WordWrap
                color: Kirigami.Theme.disabledTextColor
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                visible: shaderDialog.hasShaderEffect

                // Shader effect selector
                QQC.ComboBox {
                    id: shaderComboBox
                    Kirigami.FormData.label: i18nc("@label:listbox", "Effect:")

                    model: editorWindow._editorController ? editorWindow._editorController.availableShaders : []
                    textRole: "name"
                    valueRole: "id"

                    Accessible.name: i18nc("@label:listbox", "Shader effect")
                    Accessible.description: i18nc("@info", "Select a visual effect for zone overlays")

                    function findShaderIndex() {
                        if (!model) return 0;
                        var targetId = shaderDialog.pendingShaderId;
                        for (var i = 0; i < model.length; i++) {
                            if (model[i] && model[i].id === targetId) return i;
                        }
                        return 0;
                    }

                    Component.onCompleted: currentIndex = findShaderIndex()

                    onActivated: {
                        if (currentValue !== undefined) {
                            shaderDialog.pendingShaderId = currentValue;
                        }
                    }

                    Connections {
                        target: shaderDialog
                        function onPendingShaderIdChanged() {
                            shaderComboBox.currentIndex = shaderComboBox.findShaderIndex();
                        }
                    }

                    Connections {
                        target: editorWindow._editorController
                        function onAvailableShadersChanged() {
                            shaderComboBox.currentIndex = shaderComboBox.findShaderIndex();
                        }
                    }
                }

                // Shader description (inline, below selector)
                QQC.Label {
                    Kirigami.FormData.label: ""
                    Layout.fillWidth: true
                    visible: shaderDialog.currentShaderInfo
                        && shaderDialog.currentShaderInfo.description
                    text: shaderDialog.currentShaderInfo ? (shaderDialog.currentShaderInfo.description || "") : ""
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                    font: Kirigami.Theme.smallFont
                }

                // Author/version metadata
                QQC.Label {
                    Kirigami.FormData.label: ""
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: {
                        if (!shaderDialog.currentShaderInfo) return "";
                        var info = shaderDialog.currentShaderInfo;
                        var parts = [];
                        if (info.author) parts.push(i18nc("@info shader author", "by %1", info.author));
                        if (info.version) parts.push(i18nc("@info shader version", "v%1", info.version));
                        if (info.isUserShader) parts.push(i18nc("@info user-installed shader", "(User shader)"));
                        return parts.join(" · ");
                    }
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                    font: Kirigami.Theme.smallFont
                }
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                visible: shaderDialog.hasShaderEffect

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nc("@title:group", "Parameters")
                    visible: shaderDialog.shaderParams.length > 0
                }

                // Message when shader has no parameters
                QQC.Label {
                    Kirigami.FormData.label: ""
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    visible: shaderDialog.shaderParams.length === 0
                    text: i18nc("@info", "This effect has no configurable parameters.")
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                }

                Repeater {
                    model: shaderDialog.shaderParams

                    delegate: Loader {
                        id: paramLoader
                        Layout.fillWidth: true
                        visible: sourceComponent !== null

                        required property var modelData
                        required property int index

                        Kirigami.FormData.label: modelData.name || modelData.id

                        sourceComponent: modelData.type === "float"
                            ? floatParamRow
                            : modelData.type === "color"
                                ? colorParamRow
                                : modelData.type === "bool"
                                    ? boolParamRow
                                    : modelData.type === "int"
                                        ? intParamRow
                                        : null

                        onLoaded: {
                            if (item) {
                                item.modelData = modelData;
                            }
                        }
                    }
                }

            }
        }

        Component {
            id: floatParamRow

            RowLayout {
                property var modelData

                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC.Slider {
                    id: floatSlider
                    Layout.fillWidth: true

                    from: modelData && modelData.min !== undefined ? modelData.min : 0
                    to: modelData && modelData.max !== undefined ? modelData.max : 1
                    stepSize: modelData && modelData.step !== undefined ? modelData.step : 0.01

                    value: modelData ? shaderDialog.parameterValue(
                        modelData.id,
                        modelData.default !== undefined ? modelData.default : 0.5
                    ) : 0.5

                    Accessible.name: modelData ? (modelData.name || modelData.id) : ""
                    Accessible.description: modelData ? (modelData.description || "") : ""

                    QQC.ToolTip.text: modelData ? (modelData.description || "") : ""
                    QQC.ToolTip.visible: hovered && modelData && modelData.description
                    QQC.ToolTip.delay: Kirigami.Units.toolTipDelay

                    onMoved: {
                        if (modelData) {
                            shaderDialog.setPendingParam(modelData.id, value);
                        }
                    }
                }

                QQC.Label {
                    text: floatSlider.value.toFixed(2)
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                    horizontalAlignment: Text.AlignRight
                    font: Kirigami.Theme.smallFont
                }
            }
        }

        Component {
            id: colorParamRow

            RowLayout {
                property var modelData

                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Rectangle {
                    id: colorSwatch

                    property color currentColor: {
                        if (!modelData) return "#ffffff";
                        var colorStr = shaderDialog.parameterValue(
                            modelData.id,
                            modelData.default || "#ffffff"
                        );
                        return Qt.color(colorStr);
                    }

                    Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 1.5
                    radius: Kirigami.Units.smallSpacing
                    color: currentColor
                    border.width: 1
                    border.color: Kirigami.Theme.separatorColor

                    Accessible.name: modelData ? i18nc("@label", "%1 color", modelData.name || modelData.id) : ""
                    Accessible.role: Accessible.Button

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (!modelData) return;
                            shaderColorDialog.selectedColor = colorSwatch.currentColor;
                            shaderColorDialog.paramId = modelData.id;
                            shaderColorDialog.paramName = modelData.name || modelData.id;
                            shaderColorDialog.open();
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                QQC.Label {
                    text: colorSwatch.currentColor.toString().toUpperCase()
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                    horizontalAlignment: Text.AlignRight
                    color: Kirigami.Theme.disabledTextColor
                    font: Kirigami.Theme.smallFont
                }
            }
        }

        Component {
            id: boolParamRow

            RowLayout {
                property var modelData

                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC.CheckBox {
                    checked: modelData ? shaderDialog.parameterValue(
                        modelData.id,
                        modelData.default !== undefined ? modelData.default : false
                    ) : false

                    Accessible.name: modelData ? (modelData.name || modelData.id) : ""
                    Accessible.description: modelData ? (modelData.description || "") : ""

                    onToggled: {
                        if (modelData) {
                            shaderDialog.setPendingParam(modelData.id, checked);
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        Component {
            id: intParamRow

            RowLayout {
                property var modelData

                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC.SpinBox {
                    from: modelData && modelData.min !== undefined ? modelData.min : 0
                    to: modelData && modelData.max !== undefined ? modelData.max : 100

                    value: modelData ? shaderDialog.parameterValue(
                        modelData.id,
                        modelData.default !== undefined ? modelData.default : 0
                    ) : 0

                    Accessible.name: modelData ? (modelData.name || modelData.id) : ""
                    Accessible.description: modelData ? (modelData.description || "") : ""

                    onValueModified: {
                        if (modelData) {
                            shaderDialog.setPendingParam(modelData.id, value);
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }
    }

    // Shared color dialog for shader parameters
    ColorDialog {
        id: shaderColorDialog

        property string paramId: ""
        property string paramName: ""

        title: i18nc("@title:window", "Choose %1", paramName)

        onAccepted: {
            if (paramId) {
                shaderDialog.setPendingParam(paramId, selectedColor.toString());
            }
        }
    }

    Connections {
        // Layout name changed - TopBar Connections should handle this
        // Note: Shortcut sequence change handlers moved to EditorShortcuts.qml

        function onLayoutSaved() {
            // Show success notification (for both save and export)
            notifications.showSuccess(i18nc("@info", "Layout saved successfully"));
        }

        function onLayoutLoadFailed(error) {
            // Show error notification
            notifications.showError(i18nc("@info", "Failed to load layout: %1", error));
        }

        function onLayoutSaveFailed(error) {
            // Show error notification
            notifications.showError(i18nc("@info", "Failed to save layout: %1", error));
        }

        function onEditorClosed() {
            editorWindow.close();
        }

        function onSelectedZoneIdChanged() {
            // Explicitly update selectedZoneId when editorController.selectedZoneId changes
            if (editorWindow._editorController) {
                var newZoneId = editorWindow._editorController.selectedZoneId || "";
                editorWindow.selectedZoneId = newZoneId;
            }
        }

        function onZoneColorChanged(zoneId) {
            // When zone color changes, force selectedZone re-evaluation
            if (zoneId === editorWindow.selectedZoneId && editorWindow._editorController) {
                // Force selectedZone property to re-evaluate by accessing zones
                var _ = editorWindow._editorController.zones;
                editorWindow.selectedZoneId = editorWindow.selectedZoneId; // Trigger property update
            }
        }

        function onZonesChanged() {
            // When zones list changes, force selectedZone re-evaluation
            if (editorWindow.selectedZoneId && editorWindow._editorController) {
                // Force selectedZone property to re-evaluate
                var _ = editorWindow._editorController.zones;
                editorWindow.selectedZoneId = editorWindow.selectedZoneId; // Trigger property update
            }
        }

        function onLayoutNameChanged() {
        }

        target: editorWindow._editorController
        enabled: editorWindow._editorController !== null
    }

}
