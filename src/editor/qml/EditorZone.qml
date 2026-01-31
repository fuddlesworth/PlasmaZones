// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Editable zone component with drag and resize handles
 *
 * Provides visual representation of a zone with:
 * - Drag-to-move functionality
 * - Resize handles on corners and edges
 * - Context menu for zone operations
 * - Visual feedback (selection, hover states)
 *
 * Uses zone IDs for stable selection and visual proxy during operations.
 */
Item {
    // Wait for C++ signal to update visuals
    // Minimum zone size in pixels - acceptable as hardcoded
    // Divider operation ended, sync from model

    id: root

    // State machine for operations
    enum State {
        Idle,
        Dragging,
        Resizing
    }

    // Properties from parent
    required property real canvasWidth
    required property real canvasHeight
    required property var zoneData
    required property string zoneId
    required property bool isSelected
    property bool isPartOfMultiSelection: false // True when zone is selected AND multiple zones are selected
    property var controller: null
    property real zoneSpacing: 0  // Gap between adjacent zones (applied as zoneSpacing/2 per side)
    property real edgeGap: 0      // Gap at screen edges
    property var snapIndicator: null
    property int operationState: EditorZone.State.Idle
    // Track if this zone is part of an active divider operation
    // When true, syncFromZoneData() is blocked to prevent overwriting divider updates
    property bool isDividerOperation: false
    // Track if we're in an animated fill operation (menu/button triggered)
    // When true, onZoneGeometryChanged is blocked to prevent interrupting the animation
    property bool isAnimatingFill: false
    // Visual position properties
    // Synced from model when idle, overridden during operations, or updated from C++
    // visualWidth/Height must be greater than zoneSpacing to avoid negative dimensions
    // Initialize to default values, set immediately by syncFromZoneData()
    property real visualX: 0
    property real visualY: 0
    property real visualWidth: 0
    property real visualHeight: 0
    // Fill preview animation - enabled only during fill preview transitions
    property bool animateFillPreview: false
    // Computed property to ensure dimensions are always valid
    readonly property bool hasValidDimensions: isFinite(visualWidth) && isFinite(visualHeight) && visualWidth > zoneSpacing && visualHeight > zoneSpacing && canvasWidth > 0 && canvasHeight > 0
    // Constants
    readonly property int handleSize: Kirigami.Units.gridUnit * 1.5
    // Use theme spacing (12px)
    readonly property int minSize: 50
    // Track if mouse is over zone or any controls
    property bool mouseOverZone: hoverArea.containsMouse || anyButtonHovered || anyHandleHovered
    property bool anyButtonHovered: false // Will be set by buttons
    property bool anyHandleHovered: false // Will be set by handles

    // Signals
    signal clicked(var event)
    // Pass mouse event for modifier key handling (Ctrl+click, Shift+click)
    signal contextMenuRequested()
    // Emitted when context menu should be shown
    signal geometryChanged(real x, real y, real width, real height)
    signal deleteRequested()
    signal duplicateRequested()
    signal splitHorizontalRequested()
    signal splitVerticalRequested()
    signal expandToFillRequested()
    signal expandToFillWithCoords(real mouseX, real mouseY) // Pass zone center for consistent algorithm
    signal deleteWithFillRequested()
    signal bringToFrontRequested()
    signal sendToBackRequested()
    signal bringForwardRequested()
    signal sendBackwardRequested()
    signal operationStarted(string zoneId, real x, real y, real width, real height)
    signal operationUpdated(string zoneId, real x, real y, real width, real height)
    signal operationEnded(string zoneId)

    // Coordinate conversion helpers
    function toCanvasX(relX) {
        if (!canvasWidth || canvasWidth <= 0 || !isFinite(canvasWidth))
            return 0;

        if (relX === undefined || relX === null || !isFinite(relX) || isNaN(relX))
            return 0;

        var result = relX * canvasWidth;
        return isFinite(result) && !isNaN(result) ? result : 0;
    }

    function toCanvasY(relY) {
        if (!canvasHeight || canvasHeight <= 0 || !isFinite(canvasHeight))
            return 0;

        if (relY === undefined || relY === null || !isFinite(relY) || isNaN(relY))
            return 0;

        var result = relY * canvasHeight;
        return isFinite(result) && !isNaN(result) ? result : 0;
    }

    function toCanvasW(relW) {
        if (!canvasWidth || canvasWidth <= 0 || !isFinite(canvasWidth))
            return 0;

        if (relW === undefined || relW === null || !isFinite(relW) || isNaN(relW))
            return 0.25 * canvasWidth;

        var result = relW * canvasWidth;
        return isFinite(result) && !isNaN(result) && result > 0 ? result : 0.25 * canvasWidth;
    }

    function toCanvasH(relH) {
        if (!canvasHeight || canvasHeight <= 0 || !isFinite(canvasHeight))
            return 0;

        if (relH === undefined || relH === null || !isFinite(relH) || isNaN(relH))
            return 0.25 * canvasHeight;

        var result = relH * canvasHeight;
        return isFinite(result) && !isNaN(result) && result > 0 ? result : 0.25 * canvasHeight;
    }

    function toRelativeX(canvasX) {
        if (!canvasWidth || canvasWidth <= 0 || !isFinite(canvasWidth))
            return 0;

        if (canvasX === undefined || canvasX === null || !isFinite(canvasX) || isNaN(canvasX))
            return 0;

        var result = canvasX / canvasWidth;
        return isFinite(result) && !isNaN(result) ? result : 0;
    }

    function toRelativeY(canvasY) {
        if (!canvasHeight || canvasHeight <= 0 || !isFinite(canvasHeight))
            return 0;

        if (canvasY === undefined || canvasY === null || !isFinite(canvasY) || isNaN(canvasY))
            return 0;

        var result = canvasY / canvasHeight;
        return isFinite(result) && !isNaN(result) ? result : 0;
    }

    function toRelativeW(canvasW) {
        if (!canvasWidth || canvasWidth <= 0 || !isFinite(canvasWidth))
            return 0;

        if (canvasW === undefined || canvasW === null || !isFinite(canvasW) || isNaN(canvasW))
            return 0;

        var result = canvasW / canvasWidth;
        return isFinite(result) && !isNaN(result) && result > 0 ? result : 0;
    }

    function toRelativeH(canvasH) {
        if (!canvasHeight || canvasHeight <= 0 || !isFinite(canvasHeight))
            return 0;

        if (canvasH === undefined || canvasH === null || !isFinite(canvasH) || isNaN(canvasH))
            return 0;

        var result = canvasH / canvasHeight;
        return isFinite(result) && !isNaN(result) && result > 0 ? result : 0;
    }

    // Public function delegated to geometrySync
    function syncFromZoneData() {
        geometrySync.syncFromZoneData();
    }

    // Delegate to geometrySync
    function ensureDimensionsInitialized() {
        geometrySync.ensureDimensionsInitialized();
    }

    // Public functions delegated to fillAnimator
    function startFillAnimation(targetX, targetY, targetWidth, targetHeight) {
        fillAnimator.startFillAnimation(targetX, targetY, targetWidth, targetHeight);
    }

    function animatedExpandToFill() {
        fillAnimator.animatedExpandToFill();
    }

    focus: false
    // Apply differentiated gaps: edgeGap at screen boundaries, zoneSpacing/2 between zones
    // Detect screen boundaries using relative coordinates (tolerance 0.01)
    readonly property real edgeTolerance: 0.01
    // Zone data uses x, y, width, height (relative coordinates 0-1)
    readonly property real relX: zoneData ? (zoneData.x || 0) : 0
    readonly property real relY: zoneData ? (zoneData.y || 0) : 0
    readonly property real relWidth: zoneData ? (zoneData.width || 0) : 0
    readonly property real relHeight: zoneData ? (zoneData.height || 0) : 0
    // Calculate gap for each edge: edgeGap if at screen boundary, zoneSpacing/2 otherwise
    readonly property real leftGap: relX < edgeTolerance ? edgeGap : zoneSpacing / 2
    readonly property real topGap: relY < edgeTolerance ? edgeGap : zoneSpacing / 2
    readonly property real rightGap: (relX + relWidth) > (1.0 - edgeTolerance) ? edgeGap : zoneSpacing / 2
    readonly property real bottomGap: (relY + relHeight) > (1.0 - edgeTolerance) ? edgeGap : zoneSpacing / 2
    // Position with differentiated gaps
    x: visualX + leftGap
    y: visualY + topGap
    width: Math.max(0, visualWidth - leftGap - rightGap)
    height: Math.max(0, visualHeight - topGap - bottomGap)
    // Watch for canvas size changes - sync when dimensions become valid
    onCanvasWidthChanged: {
        Qt.callLater(ensureDimensionsInitialized);
    }
    onCanvasHeightChanged: {
        Qt.callLater(ensureDimensionsInitialized);
    }
    // Let onZoneGeometryChanged handle updates after operations
    onOperationStateChanged: {
    }
    // Watch for divider operation state changes
    onIsDividerOperationChanged: {
        if (!isDividerOperation && operationState === EditorZone.State.Idle)
            Qt.callLater(syncFromZoneData);

    }
    // Initialize visual properties when zoneData changes
    onZoneDataChanged: {
        if (root.operationState !== EditorZone.State.Idle || root.isDividerOperation)
            return ;

        // Update color trackers when zoneData changes
        if (zoneData) {
            zoneRect._highlightColorTracker = zoneData.highlightColor;
            zoneRect._inactiveColorTracker = zoneData.inactiveColor;
            zoneRect._borderColorTracker = zoneData.borderColor;
            zoneRect._activeOpacityTracker = zoneData.activeOpacity;
            zoneRect._inactiveOpacityTracker = zoneData.inactiveOpacity;
            zoneRect._borderWidthTracker = zoneData.borderWidth;
            zoneRect._borderRadiusTracker = zoneData.borderRadius;
            zoneRect._useCustomColorsTracker = zoneData.useCustomColors;
        }
        // Only sync if canvas dimensions are valid and dimensions need initialization
        if (canvasWidth > 0 && canvasHeight > 0 && isFinite(canvasWidth) && isFinite(canvasHeight)) {
            if (visualWidth === 0 || visualHeight === 0 || !hasValidDimensions)
                Qt.callLater(syncFromZoneData);

        }
    }
    // Initialize visual properties on component creation
    Component.onCompleted: {
        // Delegate to geometrySync for initialization
        ensureDimensionsInitialized();
    }
    // Cleanup on destruction
    Component.onDestruction: {
        if (operationState !== EditorZone.State.Idle)
            operationState = EditorZone.State.Idle;

    }
    // Handle context menu signal from drag handler
    onContextMenuRequested: {
        contextMenu.popup();
    }

    // ═══════════════════════════════════════════════════════════════════
    // GEOMETRY SYNC - Extracted to ZoneGeometrySync.qml
    // ═══════════════════════════════════════════════════════════════════
    ZoneGeometrySync {
        id: geometrySync

        zoneRoot: root
        controller: root.controller
    }

    // ═══════════════════════════════════════════════════════════════════
    // FILL ANIMATION - Extracted to ZoneFillAnimation.qml
    // ═══════════════════════════════════════════════════════════════════
    ZoneFillAnimation {
        id: fillAnimator

        zoneRoot: root
        controller: root.controller
        canvasWidth: root.canvasWidth
        canvasHeight: root.canvasHeight
    }

    // Zone background
    Rectangle {
        id: zoneRect

        // Use custom colors if enabled, otherwise use theme colors
        // Binding depends on tracker to force re-evaluation when useCustomColors changes
        property bool useCustom: {
            var _ = _useCustomColorsTracker; // Dependency on tracker
            var _zone = zoneData; // Dependency on zoneData
            return _zone && _zone.useCustomColors === true;
        }
        // Color trackers to force re-evaluation when colors change
        // QVariantMap property changes don't automatically trigger QML bindings
        property var _highlightColorTracker: zoneData ? zoneData.highlightColor : null
        property var _inactiveColorTracker: zoneData ? zoneData.inactiveColor : null
        property var _borderColorTracker: zoneData ? zoneData.borderColor : null
        property var _activeOpacityTracker: zoneData ? zoneData.activeOpacity : null
        property var _inactiveOpacityTracker: zoneData ? zoneData.inactiveOpacity : null
        property var _borderWidthTracker: zoneData ? zoneData.borderWidth : null
        property var _borderRadiusTracker: zoneData ? zoneData.borderRadius : null
        property var _useCustomColorsTracker: zoneData ? zoneData.useCustomColors : null
        // Bindings that depend on trackers to force re-evaluation
        property color customHighlightColor: {
            var _ = _highlightColorTracker; // Dependency on tracker
            var _zone = zoneData; // Dependency on zoneData
            return _zone && _zone.highlightColor ? parseColor(_zone.highlightColor) : Qt.transparent;
        }
        property color customInactiveColor: {
            var _ = _inactiveColorTracker; // Dependency on tracker
            var _zone = zoneData; // Dependency on zoneData
            return _zone && _zone.inactiveColor ? parseColor(_zone.inactiveColor) : Qt.transparent;
        }
        property color customBorderColor: {
            var _ = _borderColorTracker; // Dependency on tracker
            var _zone = zoneData; // Dependency on zoneData
            return _zone && _zone.borderColor ? parseColor(_zone.borderColor) : Qt.transparent;
        }
        property real customActiveOpacity: {
            var _ = _activeOpacityTracker; // Dependency on tracker
            var _zone = zoneData; // Dependency on zoneData
            return _zone && _zone.activeOpacity !== undefined ? _zone.activeOpacity : 0.5;
        }
        property real customInactiveOpacity: {
            var _ = _inactiveOpacityTracker; // Dependency on tracker
            var _zone = zoneData; // Dependency on zoneData
            return _zone && _zone.inactiveOpacity !== undefined ? _zone.inactiveOpacity : 0.3;
        }
        property int customBorderWidth: {
            var _ = _borderWidthTracker; // Dependency on tracker
            var _zone = zoneData; // Dependency on zoneData
            return _zone && _zone.borderWidth !== undefined ? _zone.borderWidth : 2;
        }
        property int customBorderRadius: {
            var _ = _borderRadiusTracker; // Dependency on tracker
            var _zone = zoneData; // Dependency on zoneData
            return _zone && _zone.borderRadius !== undefined ? _zone.borderRadius : (Kirigami.Units.smallSpacing * 1.5);
        }

        // Helper function to parse color (handles both hex strings and QColor objects)
        // Handles ARGB hex format from QColor::HexArgb
        function parseColor(colorValue) {
            if (!colorValue)
                return Qt.transparent;

            if (typeof colorValue === 'string') {
                // Check if it's ARGB format (starts with # and has 8 or 4 hex chars)
                var hex = colorValue.replace('#', '');
                if (hex.length === 8) {
                    // Parse ARGB: AARRGGBB
                    var a = parseInt(hex.substring(0, 2), 16) / 255;
                    var r = parseInt(hex.substring(2, 4), 16) / 255;
                    var g = parseInt(hex.substring(4, 6), 16) / 255;
                    var b = parseInt(hex.substring(6, 8), 16) / 255;
                    return Qt.rgba(r, g, b, a);
                } else if (hex.length === 4) {
                    // Parse ARGB shorthand: ARGB
                    var a = parseInt(hex.substring(0, 1), 16) / 15;
                    var r = parseInt(hex.substring(1, 2), 16) / 15;
                    var g = parseInt(hex.substring(2, 3), 16) / 15;
                    var b = parseInt(hex.substring(3, 4), 16) / 15;
                    return Qt.rgba(r, g, b, a);
                } else {
                    // Try Qt.color() as fallback (might be RGB format)
                    return Qt.color(colorValue);
                }
            }
            return colorValue;
        }

        anchors.fill: parent
        // Combine color's alpha channel with opacity slider: final alpha = color.a * opacity
        // This allows both color picker alpha AND opacity slider to affect the result
        // Uses separate active/inactive opacity values
        color: useCustom ? (isSelected ? Qt.rgba(customHighlightColor.r, customHighlightColor.g, customHighlightColor.b, customHighlightColor.a * customActiveOpacity) : Qt.rgba(customInactiveColor.r, customInactiveColor.g, customInactiveColor.b, customInactiveColor.a * customInactiveOpacity)) : (isSelected ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.3) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15))
        border.color: useCustom ? customBorderColor : (isSelected ? Kirigami.Theme.highlightColor : (hoverArea.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.5) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)))
        border.width: useCustom ? customBorderWidth : (isSelected ? 3 : 2)
        radius: useCustom ? customBorderRadius : (Kirigami.Units.smallSpacing * 1.5)
        // Accessibility: Screen reader announcements
        // Accessible.role is optional
        // Removing role to avoid enumeration issues in QML
        Accessible.name: root.zoneData && root.zoneData.name ? i18nc("@info:accessibility", "Zone %1: %2", root.zoneData.zoneNumber || 1, root.zoneData.name) : i18nc("@info:accessibility", "Zone %1", root.zoneData ? (root.zoneData.zoneNumber || 1) : 0)
        Accessible.description: isSelected ? i18nc("@info:accessibility", "Selected zone. Position: %1% × %2%, Size: %3% × %4%. Click to deselect, drag to move, use handles to resize.", Math.round((root.visualX / root.canvasWidth) * 100), Math.round((root.visualY / root.canvasHeight) * 100), Math.round((root.visualWidth / root.canvasWidth) * 100), Math.round((root.visualHeight / root.canvasHeight) * 100)) : i18nc("@info:accessibility", "Zone. Position: %1% × %2%, Size: %3% × %4%. Click to select.", Math.round((root.visualX / root.canvasWidth) * 100), Math.round((root.visualY / root.canvasHeight) * 100), Math.round((root.visualWidth / root.canvasWidth) * 100), Math.round((root.visualHeight / root.canvasHeight) * 100))
        Accessible.selectable: true
        Accessible.selected: root.isSelected

        // Multi-selection indicator badge (checkmark in top-left corner)
        Rectangle {
            id: multiSelectBadge

            visible: root.isPartOfMultiSelection
            width: Kirigami.Units.gridUnit
            height: Kirigami.Units.gridUnit
            radius: width / 2
            color: Kirigami.Theme.highlightColor

            anchors {
                top: parent.top
                left: parent.left
                margins: Kirigami.Units.smallSpacing
            }

            Text {
                anchors.centerIn: parent
                text: "\u2713" // Unicode checkmark
                color: Kirigami.Theme.highlightedTextColor
                font.pixelSize: parent.width * 0.6
                font.bold: true
            }

            Behavior on opacity {
                NumberAnimation {
                    duration: 100
                }

            }

        }

        Behavior on color {
            ColorAnimation {
                duration: 100
            }

        }

        Behavior on border.color {
            ColorAnimation {
                duration: 100
            }

        }

    }

    // Zone content (number and name labels)
    ZoneContent {
        anchors.fill: parent
        zoneData: root.zoneData
    }

    // Listen for zone data changes - when zonesChanged is emitted, the Repeater model updates
    // and zoneData (modelData) should automatically update, triggering property bindings
    // However, QVariantMap property changes don't automatically trigger QML bindings,
    // so we need to force re-evaluation by updating tracker properties
    Connections {
        function onZoneColorChanged(zoneId) {
            // When zone color changes for this zone, force color property re-evaluation
            if (zoneId === root.zoneId && root.zoneData) {
                // Capture references before deferred call
                var rootRef = root;
                var zoneRectRef = zoneRect;
                Qt.callLater(function() {
                    if (rootRef && rootRef.zoneData && zoneRectRef) {
                        zoneRectRef._highlightColorTracker = rootRef.zoneData.highlightColor;
                        zoneRectRef._inactiveColorTracker = rootRef.zoneData.inactiveColor;
                        zoneRectRef._borderColorTracker = rootRef.zoneData.borderColor;
                        zoneRectRef._activeOpacityTracker = rootRef.zoneData.activeOpacity;
                        zoneRectRef._inactiveOpacityTracker = rootRef.zoneData.inactiveOpacity;
                        zoneRectRef._borderWidthTracker = rootRef.zoneData.borderWidth;
                        zoneRectRef._borderRadiusTracker = rootRef.zoneData.borderRadius;
                        zoneRectRef._useCustomColorsTracker = rootRef.zoneData.useCustomColors;
                    }
                });
            }
        }

        function onZonesChanged() {
            // Zones list changed - Repeater model updates automatically
            // Force re-evaluation of all appearance properties
            if (root.zoneData) {
                // Capture references before deferred call
                var rootRef = root;
                var zoneRectRef = zoneRect;
                Qt.callLater(function() {
                    if (rootRef && rootRef.zoneData && zoneRectRef) {
                        zoneRectRef._highlightColorTracker = rootRef.zoneData.highlightColor;
                        zoneRectRef._inactiveColorTracker = rootRef.zoneData.inactiveColor;
                        zoneRectRef._borderColorTracker = rootRef.zoneData.borderColor;
                        zoneRectRef._activeOpacityTracker = rootRef.zoneData.activeOpacity;
                        zoneRectRef._inactiveOpacityTracker = rootRef.zoneData.inactiveOpacity;
                        zoneRectRef._borderWidthTracker = rootRef.zoneData.borderWidth;
                        zoneRectRef._borderRadiusTracker = rootRef.zoneData.borderRadius;
                        zoneRectRef._useCustomColorsTracker = rootRef.zoneData.useCustomColors;
                    }
                });
            }
        }

        target: root.controller
        enabled: root.controller !== null
    }

    // ═══════════════════════════════════════════════════════════════════
    // DRAG HANDLER - Extracted to ZoneDragHandler.qml
    // ═══════════════════════════════════════════════════════════════════
    ZoneDragHandler {
        id: hoverArea

        zoneRoot: root
        controller: root.controller
        snapIndicator: root.snapIndicator
    }

    // Hover action buttons (top-right)
    ActionButtons {
        id: hoverButtons

        root: root
    }

    // ═══════════════════════════════════════════════════════════════════
    // RESIZE HANDLES
    // ═══════════════════════════════════════════════════════════════════
    ResizeHandles {
        root: root
        // z-index must be higher than hoverArea so handles receive mouse events first
        z: 100
        canvasWidth: root.canvasWidth
        canvasHeight: root.canvasHeight
        handleSize: handleSize
        minSize: minSize
        zoneData: zoneData
        snapIndicator: root.snapIndicator
    }

    // ═══════════════════════════════════════════════════════════════════
    // CONTEXT MENU - Extracted to ZoneContextMenu.qml
    // ═══════════════════════════════════════════════════════════════════
    ZoneContextMenu {
        id: contextMenu

        editorController: root.controller
        zoneId: root.zoneId
        onSplitHorizontalRequested: root.splitHorizontalRequested()
        onSplitVerticalRequested: root.splitVerticalRequested()
        onDuplicateRequested: root.duplicateRequested()
        onDeleteRequested: root.deleteRequested()
        onDeleteWithFillRequested: root.deleteWithFillRequested()
        onFillRequested: root.animatedExpandToFill()
        onBringToFrontRequested: root.bringToFrontRequested()
        onBringForwardRequested: root.bringForwardRequested()
        onSendBackwardRequested: root.sendBackwardRequested()
        onSendToBackRequested: root.sendToBackRequested()
    }

    Behavior on visualX {
        enabled: root.animateFillPreview

        NumberAnimation {
            duration: 150
            easing.type: Easing.OutCubic
        }

    }

    Behavior on visualY {
        enabled: root.animateFillPreview

        NumberAnimation {
            duration: 150
            easing.type: Easing.OutCubic
        }

    }

    Behavior on visualWidth {
        enabled: root.animateFillPreview

        NumberAnimation {
            duration: 150
            easing.type: Easing.OutCubic
        }

    }

    Behavior on visualHeight {
        enabled: root.animateFillPreview

        NumberAnimation {
            duration: 150
            easing.type: Easing.OutCubic
        }

    }

}
