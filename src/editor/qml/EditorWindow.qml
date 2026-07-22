// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import "ColorUtils.js" as ColorUtils
import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import "ThemeHelpers.js" as Theme
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/// Main editor window for zone layout editing
Window {
    id: editorWindow

    // Context properties (set in main.cpp)
    property var _editorController: editorController
    // State properties
    property string selectedZoneId: editorWindow._editorController ? (editorWindow._editorController.selectedZoneId || "") : ""
    property int selectionCount: editorWindow._editorController ? editorWindow._editorController.selectionCount : 0
    property bool hasMultipleSelection: editorWindow._editorController ? editorWindow._editorController.hasMultipleSelection : false
    property string selectionAnchorId: "" // For Shift+click range selection
    readonly property bool previewMode: editorWindow._editorController ? editorWindow._editorController.previewMode : false
    // Fullscreen editing mode - hides all panels for distraction-free editing
    property bool fullscreenMode: false
    // Zone spacing (between zones) matches zone padding (per-layout override or global setting)
    readonly property real zoneSpacing: {
        if (!editorWindow._editorController)
            return Kirigami.Units.gridUnit;

        return editorWindow._editorController.gaps.hasZonePaddingOverride ? editorWindow._editorController.gaps.zonePadding : editorWindow._editorController.gaps.globalZonePadding;
    }
    // Edge gap (at screen boundaries) - separate from zone spacing
    readonly property real edgeGap: {
        if (!editorWindow._editorController)
            return Kirigami.Units.gridUnit;

        return editorWindow._editorController.gaps.hasOuterGapOverride ? editorWindow._editorController.gaps.outerGap : editorWindow._editorController.gaps.globalOuterGap;
    }
    property var _zonesRepeater: null
    // Base of the zone stacking range inside drawingArea. Each zone sits at
    // zoneBaseZ + its zOrder, and ZoneManager keeps zOrder a dense 0..count-1
    // permutation matching the zone's index in the model, so the top zone sits
    // at zonesTopZ.
    readonly property int zoneBaseZ: 60
    // Top of the zone stacking range. DividerManager needs it to decide when it
    // can win a hit test against the zones.
    readonly property int zonesTopZ: editorWindow.zoneBaseZ + Math.max(0, zonesRepeater.count - 1)
    // Where an overlay has to sit to beat every zone and every divider inside
    // drawingArea, whatever the zone count. DividerManager takes zonesTopZ + 1
    // when the zones leave it no gap of its own.
    readonly property int canvasOverlayZ: editorWindow.zonesTopZ + 2
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
        if (!editorWindow._editorController)
            return;

        // Use C++ method which applies the Wayland setGeometry() workaround
        // (QML Window.screen assignment is a no-op on Wayland for xdg-shell surfaces)
        editorWindow._editorController.showFullScreenOnTargetScreen(editorWindow);
    }

    // Helper to check if a zone is currently selected.
    // Reads the NOTIFYable selectedZoneIds property (the controller keeps it in
    // sync with single selection too) instead of the Q_INVOKABLE isSelected():
    // an invokable carries no change notification, so bindings calling this
    // helper would never re-evaluate when the selection changes.
    function isZoneSelected(zoneId) {
        if (!zoneId || !editorWindow._editorController)
            return false;

        return editorWindow._editorController.selectedZoneIds.indexOf(zoneId) !== -1;
    }

    // Handle zone click with modifier keys for multi-selection
    function handleZoneClick(zoneId, modifiers) {
        if (!editorWindow._editorController || !zoneId)
            return;

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

    // Resolve a screen id to the label the user actually sees. The controller
    // identifies screens by id ("DP-1", or a virtual-screen id), but every
    // screen button in TopBar renders displayName with the id only as a
    // fallback, so anything naming a screen back to the user has to resolve it
    // the same way or it names a screen the user cannot match to a button.
    // Delegates to the controller's screenDisplayName() invokable, which owns
    // that resolution.
    function displayNameForScreen(screenId) {
        if (!editorWindow._editorController)
            return screenId;

        return editorWindow._editorController.screenDisplayName(screenId);
    }

    // Convert a FileDialog URL to a local filesystem path. Single URL→path
    // implementation for the editor (import/export dialogs here and
    // ShaderSettingsDialog's preset/image dialogs). decodeURIComponent is
    // required for %-encoded characters (spaces etc.); the +-quantified slash
    // regex normalizes both file:// and file:/// forms to an absolute path.
    function urlToLocalPath(url) {
        if (!url)
            return "";

        return decodeURIComponent(url.toString().replace(/^file:\/\/+/, "/"));
    }

    // Open the shared context menu for a specific zone
    function openContextMenu(zoneId) {
        sharedContextMenu.zoneId = zoneId;
        sharedContextMenu.popup();
    }

    // Push the theme-derived default zone colors to C++ (so new zones use theme
    // colors). Highlight/inactive alphas come from ThemeHelpers, the same
    // values PropertyPanel applies to its previews — but only the alphas are
    // shared: the color BASES resolve in different color sets (the panel's
    // swatches in View, this window in Window), so the pushed defaults and the
    // panel's swatches can differ. The border default is an opaque
    // frame-contrast interpolation with no alpha applied. Called on startup and
    // again on live theme changes so the C++ defaults track the current theme.
    function pushDefaultZoneColors() {
        if (!editorWindow._editorController)
            return;

        var highlightColor = Theme.withAlpha(Kirigami.Theme.highlightColor, Theme.zoneHighlightAlpha);
        var inactiveColor = Theme.withAlpha(Kirigami.Theme.disabledTextColor, Theme.zoneInactiveAlpha);
        var borderColor = Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast);
        editorWindow._editorController.setDefaultZoneColors(ColorUtils.colorToArgbHex(highlightColor), ColorUtils.colorToArgbHex(inactiveColor), ColorUtils.colorToArgbHex(borderColor));
    }

    // Window flags - fullscreen editor window on Wayland
    flags: Qt.FramelessWindowHint | Qt.WindowFullScreenButtonHint
    // Transparent window clear color — the semi-transparent visual effect is
    // provided by windowBackground below. Using alpha on Window.color causes
    // Menu/Popup surfaces (xdg_popup) to inherit the window-level transparency
    // on Wayland, making all menus see-through. A child Rectangle achieves the
    // same visual without affecting popup surface compositing.
    color: "transparent"
    // Start hidden — on Wayland the compositor assigns the output when the surface
    // is first mapped. We must set the target screen BEFORE showing, otherwise the
    // window always lands on the primary monitor.
    visible: false
    title: i18nc("@title", "Layout Editor")
    Component.onCompleted: {
        // Initialize selectedZoneId from editorController context property
        if (editorWindow._editorController) {
            editorWindow.selectedZoneId = editorWindow._editorController.selectedZoneId || "";
            editorWindow.pushDefaultZoneColors();
            // If no layout loaded and not in preview mode, create new
            if (editorWindow._editorController.layoutId === "" && !editorWindow.previewMode)
                editorWindow._editorController.createNewLayout();

            // Set screen and show via C++ Q_INVOKABLE — QML Window.screen assignment
            // doesn't reliably call QWindow::setScreen() on Wayland (type mismatch)
            editorWindow._editorController.showFullScreenOnTargetScreen(editorWindow);
        }
        // Request focus on drawingArea for keyboard navigation
        // Use a timer to ensure focus is set after window is fully shown
        Qt.callLater(function () {
            drawingArea.forceActiveFocus();
        });
    }
    // Ensure drawingArea has focus when window becomes visible
    onVisibleChanged: {
        if (visible)
            Qt.callLater(function () {
                drawingArea.forceActiveFocus();
            });
    }

    // Semi-transparent background — replaces the old Window.color alpha=0.7.
    // A child Rectangle lets the window surface be fully transparent while
    // popup menus (xdg_popup) get their own opaque surfaces from the style.
    Rectangle {
        id: windowBackground

        anchors.fill: parent
        z: -1
        color: Theme.withAlpha(Kirigami.Theme.backgroundColor, 0.7)
    }

    ZoneOperations {
        id: zoneOps
    }

    // Re-push the theme-derived defaults when the theme changes at runtime, so
    // zones created after a live color-scheme switch pick up the new theme.
    Connections {
        function onColorsChanged() {
            editorWindow.pushDefaultZoneColors();
        }

        target: Kirigami.Theme
    }

    // ═══════════════════════════════════════════════════════════════════
    // SHARED CONTEXT MENU — Single instance to avoid QQmlData crash
    // When a QVariantList-backed Repeater destroys delegates, per-delegate
    // Menu/MenuItem objects (with KDE desktop style helpers) can trigger
    // a use-after-free in QQmlData::destroyed(). A shared Menu at the
    // Window level is never destroyed during model updates.
    // ═══════════════════════════════════════════════════════════════════
    ZoneContextMenu {
        id: sharedContextMenu

        editorController: editorWindow._editorController
        zoneId: ""
        // Defer model-mutating operations to the next event loop iteration.
        // Qt 6 Menu.dismiss() tears down the popup in the same tick as
        // onTriggered; if the handler modifies the Repeater model
        // synchronously, finalizeExitTransition walks stale child pointers
        // and crashes in derefWindow (QTBUG-white-paper popup UAF).
        onSplitHorizontalRequested: {
            let id = zoneId;
            if (editorWindow._editorController && id)
                Qt.callLater(editorWindow._editorController.splitZone, id, true);
        }
        onSplitVerticalRequested: {
            let id = zoneId;
            if (editorWindow._editorController && id)
                Qt.callLater(editorWindow._editorController.splitZone, id, false);
        }
        onDuplicateRequested: {
            let id = zoneId;
            if (editorWindow._editorController && id)
                Qt.callLater(editorWindow._editorController.duplicateZone, id);
        }
        onDeleteRequested: {
            let id = zoneId;
            if (editorWindow._editorController && id)
                Qt.callLater(editorWindow._editorController.deleteZone, id);
        }
        onDeleteWithFillRequested: {
            let id = zoneId;
            if (editorWindow._editorController && id)
                Qt.callLater(zoneOps.deleteWithFillAnimation, id, editorWindow._editorController, editorWindow._zonesRepeater);
        }
        onFillRequested: {
            let id = zoneId;
            if (editorWindow._editorController && id)
                Qt.callLater(editorWindow._editorController.expandToFillSpace, id);
        }
        onBringToFrontRequested: {
            if (editorWindow._editorController && zoneId)
                editorWindow._editorController.bringToFront(zoneId);
        }
        onBringForwardRequested: {
            if (editorWindow._editorController && zoneId)
                editorWindow._editorController.bringForward(zoneId);
        }
        onSendBackwardRequested: {
            if (editorWindow._editorController && zoneId)
                editorWindow._editorController.sendBackward(zoneId);
        }
        onSendToBackRequested: {
            if (editorWindow._editorController && zoneId)
                editorWindow._editorController.sendToBack(zoneId);
        }
    }

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
        previewMode: editorWindow.previewMode
        onFullscreenToggled: editorWindow.toggleFullscreenMode()
    }

    // Keyboard navigation handler
    KeyboardNavigation {
        id: keyboardNav

        editorController: editorWindow._editorController
    }

    // ═══════════════════════════════════════════════════════════════════
    // TOP BAR
    // ═══════════════════════════════════════════════════════════════════
    TopBar {
        id: topBar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        // Fades out on the way into fullscreen, paired with the "Exit
        // Fullscreen" pill below. `visible` follows the animated opacity rather
        // than the mode, so the outgoing leg is actually rendered. mainLayout
        // re-anchors off fullscreenMode itself, so the canvas does not wait for
        // this fade and nothing jumps.
        opacity: !editorWindow.fullscreenMode ? 1 : 0
        visible: opacity > 0
        // `visible` keeps the outgoing leg on screen for the length of the fade,
        // so gate hit-testing on the mode itself: a bar on its way out must not
        // answer clicks meant for the canvas underneath.
        enabled: !editorWindow.fullscreenMode
        // Pass stored context properties to avoid scoping issues
        editorController: editorWindow._editorController
        availableScreens: editorWindow._editorController ? editorWindow._editorController.screenModel : []
        confirmCloseDialog: confirmCloseDialog
        helpDialog: helpDialog
        shaderDialog: shaderDialog
        visibilityDialog: visibilityDialog
        layoutSettingsDialog: layoutSettingsDialog
        importDialog: importDialog
        editorWindow: editorWindow
        exportDialog: exportDialog
        fullscreenMode: editorWindow.fullscreenMode
        previewMode: editorWindow.previewMode
        onFullscreenToggled: editorWindow.toggleFullscreenMode()

        Behavior on opacity {
            PhosphorMotionAnimation {
                // Pinned to widget.fadeIn for both directions. The top bar
                // and the floating "Exit Fullscreen" pill below are
                // reciprocal opacity bindings during a fullscreen toggle,
                // so they must share one profile (200 ms, widget-out
                // curve) for a clean handoff. Deliberately NOT using
                // widget.fadeOut for the 1->0 direction: its 400 ms
                // cubic-in seed is for graceful standalone exits, which
                // would desync the paired surfaces.
                profile: "widget.fadeIn"
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
            Layout.minimumWidth: Kirigami.Units.gridUnit * 22

            // Actual drawing area (zones handle their own gaps via edgeGap and zoneSpacing)
            // When !useFullScreenGeometry, insets shrink the canvas to match the usable area
            // (excluding panels/taskbars) so zone positions match the daemon's rendering.
            Item {
                // Allow Tab/Shift+Tab for standard focus navigation (accessibility requirement)

                id: drawingArea

                readonly property bool applyInsets: editorWindow._editorController ? !editorWindow._editorController.useFullScreenGeometry : false
                property bool _insetsReady: false

                objectName: "drawingArea" // Required for focus restoration from child components
                // For virtual screens, the window itself is sized to the VS region,
                // so the drawing area fills the entire window (no VS offset needed).
                anchors.fill: parent
                anchors.leftMargin: applyInsets && editorWindow._editorController ? editorWindow._editorController.insetLeft : 0
                anchors.topMargin: applyInsets && editorWindow._editorController ? editorWindow._editorController.insetTop : 0
                anchors.rightMargin: applyInsets && editorWindow._editorController ? editorWindow._editorController.insetRight : 0
                anchors.bottomMargin: applyInsets && editorWindow._editorController ? editorWindow._editorController.insetBottom : 0
                focus: true
                // Enable keyboard focus for navigation
                // Keyboard navigation - uses extracted KeyboardNavigation component
                Keys.priority: Keys.AfterItem
                // Allow standard Tab navigation first
                Keys.enabled: true
                Keys.onPressed: function (event) {
                    keyboardNav.handleKeyPress(event);
                }
                Component.onCompleted: _insetsReady = true

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
                        previewMode: editorWindow.previewMode
                        // Bind directly to modelData - when zones list updates, modelData updates
                        zoneData: modelData
                        zoneId: modelData.id || ""
                        isSelected: editorWindow.isZoneSelected(modelData.id)
                        isPartOfMultiSelection: isSelected && editorWindow.hasMultipleSelection
                        controller: editorWindow._editorController // Pass controller for snapping
                        zoneSpacing: editorWindow.zoneSpacing // Pass spacing for gaps between zones
                        edgeGap: editorWindow.edgeGap // Pass gap for screen edges
                        snapIndicator: snapIndicator // Pass snapIndicator for visual feedback
                        // Z-order: zoneBaseZ + zOrder from model. This sits above
                        // DividerManager's usual z, but not above it unconditionally:
                        // at a zero zone gap DividerManager deliberately lifts itself
                        // over every zone so its handles stay grabbable.
                        z: editorWindow.zoneBaseZ + (modelData.zOrder !== undefined ? modelData.zOrder : 0)
                        onClicked: function (event) {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow.handleZoneClick(modelData.id, event.modifiers);
                        }
                        onGeometryChanged: function (x, y, w, h, skipSnapping) {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.updateZoneGeometry(modelData.id, x, y, w, h, skipSnapping);
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
                        onExpandToFillWithCoords: function (mouseX, mouseY) {
                            if (editorWindow._editorController && modelData && modelData.id)
                                editorWindow._editorController.expandToFillSpace(modelData.id, mouseX, mouseY);
                        }
                        // Track zone operations for snap/dimension indicators
                        onOperationStarted: function (zoneId, x, y, width, height) {
                            activeZoneOperation.active = true;
                            activeZoneOperation.zoneId = zoneId;
                            activeZoneOperation.x = x;
                            activeZoneOperation.y = y;
                            activeZoneOperation.width = width;
                            activeZoneOperation.height = height;
                        }
                        onOperationUpdated: function (zoneId, x, y, width, height) {
                            activeZoneOperation.x = x;
                            activeZoneOperation.y = y;
                            activeZoneOperation.width = width;
                            activeZoneOperation.height = height;
                        }
                        onOperationEnded: function (zoneId) {
                            activeZoneOperation.active = false;
                            activeZoneOperation.zoneId = "";
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
                    previewMode: editorWindow.previewMode
                    zonesTopZ: editorWindow.zonesTopZ
                }

                // Snap line visualization
                SnapIndicator {
                    id: snapIndicator

                    // anchors.fill is set in SnapIndicator.qml to fill parent (drawingArea)
                    overlayZ: editorWindow.canvasOverlayZ
                    showSnapLines: true
                }

                // Dimension tooltip
                DimensionTooltip {
                    id: dimensionTooltip

                    overlayZ: editorWindow.canvasOverlayZ + 1
                    zoneX: activeZoneOperation.x
                    zoneY: activeZoneOperation.y
                    zoneWidth: activeZoneOperation.width
                    zoneHeight: activeZoneOperation.height
                    canvasWidth: drawingArea.width
                    canvasHeight: drawingArea.height
                    showDimensions: activeZoneOperation.active
                    isFixedMode: activeZoneOperation.isFixedZone
                    screenWidth: editorWindow._editorController ? editorWindow._editorController.targetScreenSize.width : 1920
                    screenHeight: editorWindow._editorController ? editorWindow._editorController.targetScreenSize.height : 1080
                }

                // Track active zone operation state
                QtObject {
                    id: activeZoneOperation

                    property bool active: false
                    property string zoneId: ""
                    property real x: 0
                    property real y: 0
                    property real width: 0
                    property real height: 0
                    property bool isFixedZone: {
                        if (!active || !zoneId || !editorWindow._editorController)
                            return false;

                        var zoneData = editorWindow._editorController.getZoneById(zoneId);
                        return zoneData ? (zoneData.geometryMode === 1) : false;
                    }
                }

                // ═══════════════════════════════════════════════════════════
                // CANVAS MOUSE HANDLER - Extracted to CanvasMouseHandler.qml
                // ═══════════════════════════════════════════════════════════
                CanvasMouseHandler {
                    editorWindow: editorWindow
                    editorController: editorWindow._editorController
                    drawingArea: drawingArea
                    previewMode: editorWindow.previewMode
                }

                Behavior on anchors.leftMargin {
                    enabled: drawingArea._insetsReady

                    PhosphorMotionAnimation {
                        // Inset margin tween — not a fade, use the widget
                        // family root for the generic 150 ms ease-out shape.
                        profile: "widget"
                    }
                }

                Behavior on anchors.topMargin {
                    enabled: drawingArea._insetsReady

                    PhosphorMotionAnimation {
                        profile: "widget"
                    }
                }

                Behavior on anchors.rightMargin {
                    enabled: drawingArea._insetsReady

                    PhosphorMotionAnimation {
                        profile: "widget"
                    }
                }

                Behavior on anchors.bottomMargin {
                    enabled: drawingArea._insetsReady

                    PhosphorMotionAnimation {
                        profile: "widget"
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════
        // PROPERTIES PANEL (Right side) - Stable width, opacity animation
        // ═══════════════════════════════════════════════════════════════
        PropertyPanel {
            id: propertiesPanel

            chromeVisible: !editorWindow.fullscreenMode && !editorWindow.previewMode
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
        previewMode: editorWindow.previewMode
        editorController: editorWindow._editorController
        confirmCloseDialog: confirmCloseDialog
        editorWindow: editorWindow
    }

    // ═══════════════════════════════════════════════════════════════════
    // FULLSCREEN EXIT BUTTON - Floating button to exit fullscreen mode
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id: fullscreenExitButton

        // `visible` follows the animated opacity, not the mode, or the pill
        // would be unrendered in the same pass its fade-out starts. Hit-testing
        // follows the mode instead, so a click on the fading pill cannot toggle
        // fullscreen straight back on.
        visible: opacity > 0
        enabled: editorWindow.fullscreenMode
        width: exitButtonRow.width + Kirigami.Units.gridUnit * 2
        height: Kirigami.Units.gridUnit * 3
        radius: Kirigami.Units.smallSpacing
        // Tracks hover through a binding: assigning the colour from onEntered/
        // onExited would sever this, and the pill would stop following the theme.
        color: exitButtonMouse.containsMouse ? Theme.withAlpha(Kirigami.Theme.highlightColor, 0.3) : Theme.withAlpha(Kirigami.Theme.backgroundColor, 0.9)
        // Focus ring when reached via Tab, frame-contrast hairline otherwise
        border.color: fullscreenExitButton.activeFocus ? Kirigami.Theme.focusColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
        border.width: fullscreenExitButton.activeFocus ? 2 : 1
        z: 200
        // Fade in/out animation
        opacity: editorWindow.fullscreenMode ? 1 : 0
        // Keyboard access: the pill is the only way out of fullscreen besides
        // the F11/Escape shortcuts, so it must be Tab-reachable and activatable.
        activeFocusOnTab: true
        Keys.onReturnPressed: editorWindow.toggleFullscreenMode()
        Keys.onEnterPressed: editorWindow.toggleFullscreenMode()
        Keys.onSpacePressed: editorWindow.toggleFullscreenMode()
        Accessible.role: Accessible.Button
        Accessible.name: i18nc("@action:button", "Exit fullscreen")
        Accessible.onPressAction: editorWindow.toggleFullscreenMode()

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
            id: exitButtonMouse

            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            onClicked: editorWindow.toggleFullscreenMode()
        }

        Behavior on opacity {
            PhosphorMotionAnimation {
                // Pinned to widget.fadeIn for both directions. Paired with
                // the top bar fade above; same rationale (200 ms,
                // widget-out curve, NOT widget.fadeOut's 400 ms tail) so
                // the pill and the top bar exchange visibility cleanly.
                profile: "widget.fadeIn"
            }
        }
    }

    Kirigami.PromptDialog {
        id: confirmCloseDialog

        title: i18nc("@title:window", "Unsaved Changes")
        subtitle: i18nc("@info", "You have unsaved changes. What would you like to do?")
        standardButtons: Kirigami.Dialog.Save | Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
        preferredWidth: Kirigami.Units.gridUnit * 24
        // Close only once the save has actually landed. A refused save leaves
        // the layout dirty and emits layoutSaveFailed, and closing anyway would
        // throw away the work the user pressed Save to keep. Staying open keeps
        // the editor on screen with the edits intact and the error visible.
        onAccepted: {
            if (editorWindow._editorController && !editorWindow._editorController.saveLayout())
                return;

            editorWindow.close();
        }
        onDiscarded: {
            editorWindow.close();
        }
    }

    // Screen switches replace the loaded layout, so the controller parks the
    // request when there are unsaved edits and emits
    // targetScreenChangeRequiresConfirmation instead of applying it. Every
    // button here answers the controller, so the parked screen never lingers.
    Kirigami.PromptDialog {
        id: confirmScreenSwitchDialog

        // Label of the screen the parked switch is heading for, set by the
        // handler that opens this dialog. It carries the display label rather
        // than the screen id so the prompt names the screen the same way the
        // button the user just clicked does.
        property string screenName: ""

        title: i18nc("@title:window", "Unsaved Changes")
        subtitle: i18nc("@info", "Switching to %1 will load that screen's layout. What would you like to do with your unsaved changes?", confirmScreenSwitchDialog.screenName)
        standardButtons: Kirigami.Dialog.Save | Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
        preferredWidth: Kirigami.Units.gridUnit * 24
        // Apply the parked switch only once the save has landed. The switch
        // loads the target screen's layout OVER the current one, so confirming
        // it after a refused save destroys the very edits the user pressed Save
        // to keep. Leaving the switch unconfirmed degrades correctly: onClosed
        // drops the parked screen, so the editor simply stays where it is with
        // the edits intact and layoutSaveFailed showing why.
        onAccepted: {
            if (editorWindow._editorController && editorWindow._editorController.saveLayout())
                editorWindow._editorController.confirmPendingTargetScreen();
        }
        // Kirigami's Dialog re-emits discarded() and leaves itself open — only
        // the Save path routes through accept(), which closes. Answer the
        // controller first, then close by hand, or the prompt would stay up over
        // the layout the switch has already loaded underneath it.
        onDiscarded: {
            if (editorWindow._editorController)
                editorWindow._editorController.confirmPendingTargetScreen();

            confirmScreenSwitchDialog.close();
        }
        // Cancel, Esc and click-away all land here without having answered, so
        // this is where the parked screen gets dropped. A save that FAILED
        // lands here unanswered too, which is exactly the wanted outcome: the
        // switch is abandoned and the unsaved layout stays loaded.
        // A successful Save and a Discard have already answered the controller
        // by the time this runs — Save from onAccepted before Kirigami closes
        // the dialog, Discard from the close call in onDiscarded above — and
        // answering clears the parked screen, so the call below is a no-op on
        // those paths.
        onClosed: {
            if (editorWindow._editorController)
                editorWindow._editorController.cancelPendingTargetScreen();
        }
    }

    // A forwarded launch (a second `plasmazones-editor --new` / `--layout <id>`,
    // or a settings/KCM button) wants to replace the loaded layout while there
    // are unsaved edits. The controller parks it and asks rather than applying,
    // so every button here answers it and the parked request never lingers.
    Kirigami.PromptDialog {
        id: confirmLaunchDialog

        title: i18nc("@title:window", "Unsaved Changes")
        subtitle: i18nc("@info", "Opening another layout will replace the one you are editing. What would you like to do with your unsaved changes?")
        standardButtons: Kirigami.Dialog.Save | Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
        preferredWidth: Kirigami.Units.gridUnit * 24
        // Same rule as the screen-switch prompt: apply the parked request only
        // once the save has landed, or a refused save loses the work anyway.
        onAccepted: {
            if (editorWindow._editorController && editorWindow._editorController.saveLayout())
                editorWindow._editorController.confirmPendingLaunch();
        }
        onDiscarded: {
            if (editorWindow._editorController)
                editorWindow._editorController.confirmPendingLaunch();

            confirmLaunchDialog.close();
        }
        // Cancel, Esc, click-away and a failed save all land here without
        // having answered, which is where the parked request gets dropped.
        onClosed: {
            if (editorWindow._editorController)
                editorWindow._editorController.cancelPendingLaunch();
        }
    }

    // File dialogs for import/export
    FileDialog {
        id: importDialog

        title: i18nc("@title:window", "Import Layout")
        nameFilters: [i18nc("@item:inlistbox", "JSON files (*.json)"), i18nc("@item:inlistbox", "All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (editorWindow._editorController)
                editorWindow._editorController.importLayout(editorWindow.urlToLocalPath(selectedFile));
        }
    }

    FileDialog {
        id: exportDialog

        title: i18nc("@title:window", "Export Layout")
        nameFilters: [i18nc("@item:inlistbox", "JSON files (*.json)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        onAccepted: {
            if (editorWindow._editorController)
                editorWindow._editorController.exportLayout(editorWindow.urlToLocalPath(selectedFile));
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
        editorWindow: editorWindow
    }

    // ═══════════════════════════════════════════════════════════════════
    // VISIBILITY SETTINGS DIALOG
    // ═══════════════════════════════════════════════════════════════════
    VisibilitySettingsDialog {
        id: visibilityDialog

        editorController: editorWindow._editorController
    }

    // ═══════════════════════════════════════════════════════════════════
    // LAYOUT SETTINGS DIALOG
    // ═══════════════════════════════════════════════════════════════════
    LayoutSettingsDialog {
        id: layoutSettingsDialog

        editorController: editorWindow._editorController
    }

    Connections {
        // Layout name changed - TopBar Connections should handle this
        // Note: Shortcut sequence change handlers moved to EditorShortcuts.qml

        function onLayoutSaved() {
            notifications.showSuccess(i18nc("@info", "Layout saved successfully"));
        }

        function onLayoutExported() {
            notifications.showSuccess(i18nc("@info", "Layout exported"));
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

        function onTargetScreenChanged() {
            editorWindow.moveToTargetScreen();
        }

        // The controller parked a screen switch because the current layout has
        // unsaved edits. Ask, then answer it either way — leaving the prompt
        // unanswered would strand the parked screen.
        function onTargetScreenChangeRequiresConfirmation(screenName) {
            confirmScreenSwitchDialog.screenName = editorWindow.displayNameForScreen(screenName);
            confirmScreenSwitchDialog.open();
        }

        // The controller parked a forwarded launch because the current layout
        // has unsaved edits. Same contract as the screen switch above: ask,
        // then answer it either way so the parked request never strands.
        function onLaunchRequestRequiresConfirmation() {
            confirmLaunchDialog.open();
        }

        target: editorWindow._editorController
        enabled: editorWindow._editorController !== null
    }
}
