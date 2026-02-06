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
    // Zone spacing (between zones) matches zone padding (per-layout override or global setting)
    readonly property real zoneSpacing: {
        if (!editorWindow._editorController) return Kirigami.Units.gridUnit;
        return editorWindow._editorController.hasZonePaddingOverride
            ? editorWindow._editorController.zonePadding
            : editorWindow._editorController.globalZonePadding;
    }
    // Edge gap (at screen boundaries) - separate from zone spacing
    readonly property real edgeGap: {
        if (!editorWindow._editorController) return Kirigami.Units.gridUnit;
        return editorWindow._editorController.hasOuterGapOverride
            ? editorWindow._editorController.outerGap
            : editorWindow._editorController.globalOuterGap;
    }
    property var _zonesRepeater: null
    // Helper to get selected zone data - reactive to both selectedZoneId AND zones changes
    // Uses C++ Q_INVOKABLE method for O(1) lookup instead of O(n) JavaScript loop
    property var selectedZone: {
        var controller = editorWindow._editorController;
        if (!controller)
            return null;

        var zoneId = selectedZoneId;
        if (!zoneId || zoneId === "")
            return null;

        // Use zonesVersion for dependency tracking instead of accessing zones.
        // This avoids copying the entire QVariantList just to create a binding dependency.
        // zonesVersion is a lightweight integer that increments on any zone change.
        var _ = controller.zonesVersion;
        
        // Use efficient C++ lookup instead of JavaScript loop
        try {
            var zone = controller.getZoneById(zoneId);
            return (zone && zone.id) ? zone : null;
        } catch (e) {
            console.warn("EditorWindow: Error accessing selectedZone:", e);
            return null;
        }
    }

    function toggleFullscreenMode() {
        fullscreenMode = !fullscreenMode;
    }

    // Move the editor window to the screen matching editorController.targetScreen
    function moveToTargetScreen() {
        if (!editorWindow._editorController || !editorWindow._availableScreens)
            return;
        var targetName = editorWindow._editorController.targetScreen;
        if (!targetName)
            return;
        for (var i = 0; i < editorWindow._availableScreens.length; i++) {
            if (editorWindow._availableScreens[i].name === targetName) {
                editorWindow.screen = editorWindow._availableScreens[i];
                return;
            }
        }
    }

    // Helper to check if a zone is currently selected
    // Uses C++ Q_INVOKABLE method - faster than JavaScript loop due to no JS engine overhead
    function isZoneSelected(zoneId) {
        if (!zoneId || !editorWindow._editorController)
            return false;
        return editorWindow._editorController.isSelected(zoneId);
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
        if (!controller)
            return null;

        // Use efficient C++ lookup instead of JavaScript loop
        var zone = controller.getZoneById(zoneId);
        return (zone && zone.id) ? zone : null;
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

    // Window flags - fullscreen editor window on Wayland
    flags: Qt.FramelessWindowHint | Qt.WindowFullScreenButtonHint
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
        // Move editor to the target screen (handles --screen arg and D-Bus openEditorForScreen)
        moveToTargetScreen();
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

            // Actual drawing area (zones handle their own gaps via edgeGap and zoneSpacing)
            Item {
                // Allow Tab/Shift+Tab for standard focus navigation (accessibility requirement)

                id: drawingArea

                objectName: "drawingArea" // Required for focus restoration from child components
                anchors.fill: parent
                // No margins here - zones apply their own gaps (edgeGap at screen edges, zoneSpacing/2 between zones)
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
                        zoneSpacing: editorWindow.zoneSpacing // Pass spacing for gaps between zones
                        edgeGap: editorWindow.edgeGap // Pass gap for screen edges
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
            left: parent.left
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
    ShaderSettingsDialog {
        id: shaderDialog
        editorController: editorWindow._editorController
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

        function onTargetScreenChanged() {
            editorWindow.moveToTargetScreen();
        }

        function onLayoutNameChanged() {
        }

        target: editorWindow._editorController
        enabled: editorWindow._editorController !== null
    }

}
