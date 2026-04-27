// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.plasmazones.common as QFZCommon

/**
 * Zone Selector Window - Overlay window showing layout previews
 * Appears when dragging window near screen edge
 * Styled similar to KZones for consistent KDE UX
 */
Window {
    // Do NOT requestActivate() — this surface is created with
    // KeyboardInteractivity::None (PzRoles::ZoneSelector) so the
    // compositor cannot grant it focus anyway. The call is a no-op
    // at best and may log warnings on strict compositors.
    // Phase 5: surface lifecycle + show/hide animations are entirely library-
    // driven. PhosphorAnimationLayer::SurfaceAnimator (registered for
    // PzRoles::ZoneSelector) drives this Window's content opacity via its
    // `panel.popup` / `widget.fadeOut` profiles; the PhosphorLayer::Surface
    // state machine handles `Qt.WindowTransparentForInput` on the underlying
    // QWindow during the hide cycle. The previous `_selectorDismissed` flag
    // + `showAnimation` / `hideAnimation` blocks are gone.
    // (Phase 5: showAnimation / hideAnimation removed — library drives.)

    id: root

    // Layout data (array of layout objects with id, name, zones)
    property var layouts: []
    property string activeLayoutId: ""
    property string hoveredLayoutId: ""
    // Mirrors the global "Auto-assign for all layouts" master toggle (#370).
    // Forwarded into LayoutCard so the category badge shows effective state.
    property bool globalAutoAssign: false
    // Selected zone tracking
    property string selectedLayoutId: ""
    property int selectedZoneIndex: -1
    // Minimum on-screen pixel size for a hit-testable zone. MUST stay
    // equal to the C++ hit-test clamp in overlayservice/selector.cpp
    // (updateSelectorPosition); the two values together define the
    // visual-vs-logical agreement contract.
    property int minZoneSize: 8
    // Cursor position (updated from C++ during drag)
    property int cursorX: -1
    property int cursorY: -1
    // Screen aspect ratio (set from C++ based on actual screen)
    property real screenAspectRatio: 16 / 9
    property int screenWidth: 1920 // Actual screen width for scaling
    // Selector configuration from settings
    // Fallback defaults; C++ (OverlayService) sets these with actual settings values.
    // See ConfigDefaults class for the single source of truth.
    property int selectorPosition: 0
    property int selectorLayoutMode: 1
    property int selectorGridColumns: 5
    property int previewWidth: 180
    property int previewHeight: 101
    property bool previewLockAspect: true
    property bool positionIsVertical: false
    // Layout indicator dimensions (set from C++ to keep a single source of truth)
    property int indicatorWidth: 180
    property int indicatorHeight: 101
    property int indicatorSpacing: 18
    property int layoutColumns: 1
    property int layoutRows: 1
    property int contentWidth: 180
    property int contentHeight: 129
    // Container layout constants (set from C++)
    property int containerPadding: 36
    // Padding inside container (both sides)
    property int containerPaddingSide: 18
    property int containerTopMargin: 10 // Top margin from window edge
    property int containerSideMargin: 10 // Side margin from window edge (for corners/sides)
    property int containerRadius: 12 // Container corner radius
    property int labelTopMargin: 8 // Margin between preview and label
    property int labelHeight: 20 // Approximate label height
    property int labelSpace: 28 // Total space for label below preview
    property int cardPadding: 26 // Extra vertical space for card chrome
    property int cardSidePadding: 18 // Extra horizontal space for card chrome
    property int containerWidth: 216
    property int containerHeight: 165
    property int barHeight: 175
    property int barWidth: 216
    // Scroll support properties (set from C++ when auto-sizing)
    property int totalRows: 1
    // Total rows (may exceed visible rows)
    property int scrollContentHeight: 129
    // Full content height for scrolling
    property int scrollContentWidth: 180
    // Full content width for horizontal scrolling
    property bool needsScrolling: false
    property bool needsHorizontalScrolling: false
    // Scale factor for converting screen pixels to preview pixels
    property real previewScale: 0.09375
    // Zone settings from C++ (actual pixel values)
    property int zonePadding: 0
    // Gap between zones in pixels
    property int zoneBorderWidth: 2
    // Border width in pixels
    property int zoneBorderRadius: 8
    // Scaled values for preview (computed from C++ to ensure they update correctly)
    property int scaledPadding: 1
    property int scaledBorderWidth: 1
    property int scaledBorderRadius: 2
    property bool locked: false
    // Appearance properties - unified with ZoneOverlay/ZoneItem for consistent look.
    // These values are visible only in the brief window between componentComplete
    // and the first writeColorSettings() from C++ (PlasmaZones::OverlayService).
    // They use the same numeric alphas as ConfigDefaults so a regression that
    // skips the C++ property push doesn't flash a jarringly different colour.
    readonly property real _fallbackHighlightAlpha: 0.7
    readonly property real _fallbackInactiveAlpha: 0.4
    readonly property real _fallbackBorderAlpha: 0.9
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, _fallbackHighlightAlpha)
    property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, _fallbackInactiveAlpha)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, _fallbackBorderAlpha)
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property real activeOpacity: 0.5 // Match Settings default
    property real inactiveOpacity: 0.3 // Match Settings default
    // Shared fade color for scroll edge indicators
    readonly property color fadeColor: Qt.rgba(backgroundColor.r, backgroundColor.g, backgroundColor.b, autoScrollConstants.fadeOpacity)

    // Signals (zoneSelected is used by C++ for hover-based zone selection)
    signal zoneSelected(string layoutId, int zoneIndex, var relativeGeometry)

    // Scroll the zone selector from C++ (D-Bus forwarded wheel/keyboard events)
    function applyScrollDelta(angleDeltaY) {
        var step = angleDeltaY / 120 * 0.1;
        if (root.needsScrolling) {
            var vBar = scrollView.ScrollBar.vertical;
            vBar.position = Math.max(0, Math.min(1 - vBar.size, vBar.position - step));
        } else if (root.needsHorizontalScrolling) {
            var hBar = scrollView.ScrollBar.horizontal;
            hBar.position = Math.max(0, Math.min(1 - hBar.size, hBar.position - step));
        }
    }

    /// Reset transient hover state. C++ calls this from hideZoneSelector
    /// before invoking Surface::hide() so the autoScrollTimer doesn't tick
    /// on stale coordinates between hide and the next show().
    function resetCursorState() {
        root.cursorX = -1;
        root.cursorY = -1;
    }

    // Window configuration. Static flags — Phase 5 surface lifecycle owns
    // `Qt.WindowTransparentForInput` on the underlying QWindow during hide.
    flags: Qt.FramelessWindowHint | Qt.Tool
    color: "transparent"
    visible: false

    // Animation constants (kept for non-show/hide bindings inside this file
    // that still reference shortDuration / normalDuration).
    QtObject {
        id: animationConstants

        readonly property int shortDuration: 150
        readonly property int normalDuration: 200
    }

    // Auto-scroll constants for drag-based scrolling.
    // During window drag the compositor captures the pointer, so scroll wheel
    // events can't reach QML.  Auto-scroll triggers when the cursor is near
    // the edges of the scrollable area.
    QtObject {
        id: autoScrollConstants

        readonly property int edgeMargin: Kirigami.Units.gridUnit * 6 // ~48px trigger zone
        readonly property real maxSpeed: 0.07 // max scroll position change per tick (0-1 range)
        readonly property int interval: 16 // ~60fps
        readonly property real scrollThreshold: 0.01 // epsilon for scroll position comparisons
        readonly property real fadeOpacity: 0.95 // fade gradient edge opacity (matches container)
    }

    Timer {
        id: autoScrollTimer

        interval: autoScrollConstants.interval
        repeat: true
        running: root.visible && (root.needsScrolling || root.needsHorizontalScrolling) && root.cursorX >= 0
        onTriggered: {
            // Map window-local cursor coords to scrollView-local coordinates
            // cursorX/cursorY are already window-local (C++ does mapFromGlobal before setting them)
            var local = scrollView.mapFromItem(null, root.cursorX, root.cursorY);
            var lx = local.x;
            var ly = local.y;
            // Vertical auto-scroll
            if (root.needsScrolling) {
                var vBar = scrollView.ScrollBar.vertical;
                if (ly >= 0 && ly < autoScrollConstants.edgeMargin) {
                    // Near top edge — scroll up, speed proportional to closeness
                    var topFactor = 1 - ly / autoScrollConstants.edgeMargin;
                    vBar.position = Math.max(0, vBar.position - autoScrollConstants.maxSpeed * topFactor);
                } else if (ly > scrollView.height - autoScrollConstants.edgeMargin && ly <= scrollView.height) {
                    // Near bottom edge — scroll down
                    var bottomFactor = 1 - (scrollView.height - ly) / autoScrollConstants.edgeMargin;
                    var maxPos = 1 - vBar.size;
                    vBar.position = Math.min(maxPos, vBar.position + autoScrollConstants.maxSpeed * bottomFactor);
                }
            }
            // Horizontal auto-scroll
            if (root.needsHorizontalScrolling) {
                var hBar = scrollView.ScrollBar.horizontal;
                if (lx >= 0 && lx < autoScrollConstants.edgeMargin) {
                    // Near left edge — scroll left
                    var leftFactor = 1 - lx / autoScrollConstants.edgeMargin;
                    hBar.position = Math.max(0, hBar.position - autoScrollConstants.maxSpeed * leftFactor);
                } else if (lx > scrollView.width - autoScrollConstants.edgeMargin && lx <= scrollView.width) {
                    // Near right edge — scroll right
                    var rightFactor = 1 - (scrollView.width - lx) / autoScrollConstants.edgeMargin;
                    var maxHPos = 1 - hBar.size;
                    hBar.position = Math.min(maxHPos, hBar.position + autoScrollConstants.maxSpeed * rightFactor);
                }
            }
        }
    }

    // Content wrapper. Opacity defaults to 1 — Phase 5 SurfaceAnimator
    // drives Window.contentItem opacity for show/hide so this child stays
    // at 1 and inherits visibility from the parent fade. Animating
    // root.opacity directly would emit Wayland setOpacity warnings on
    // layer-shell surfaces.
    Item {
        id: contentWrapper

        anchors.fill: parent

        // Main container - uses States for proper anchor management
        // Conditional anchors with undefined don't reliably unset in QML
        QFZCommon.PopupFrame {
            // Scroll fade indicators — show gradient edges when content overflows

            id: container

            objectName: "zoneSelectorContainer"
            width: root.containerWidth
            height: root.containerHeight
            backgroundColor: root.backgroundColor
            textColor: root.textColor
            containerRadius: root.containerRadius
            // State based on selectorPosition (0-8 grid)
            // 0=TopLeft, 1=Top, 2=TopRight, 3=Left, 4=Center, 5=Right, 6=BottomLeft, 7=Bottom, 8=BottomRight
            state: {
                switch (root.selectorPosition) {
                case 0:
                    return "topLeft";
                case 1:
                    return "top";
                case 2:
                    return "topRight";
                case 3:
                    return "left";
                case 4:
                    return "center";
                case 5:
                    return "right";
                case 6:
                    return "bottomLeft";
                case 7:
                    return "bottom";
                case 8:
                    return "bottomRight";
                default:
                    return "top";
                }
            }
            states: [
                State {
                    name: "topLeft"

                    AnchorChanges {
                        target: container
                        anchors.top: parent.top
                        anchors.bottom: undefined
                        anchors.left: parent.left
                        anchors.right: undefined
                        anchors.verticalCenter: undefined
                        anchors.horizontalCenter: undefined
                    }

                    PropertyChanges {
                        target: container
                        anchors.topMargin: root.containerTopMargin
                        anchors.leftMargin: root.containerSideMargin
                    }

                },
                State {
                    name: "top"

                    AnchorChanges {
                        target: container
                        anchors.top: parent.top
                        anchors.bottom: undefined
                        anchors.left: undefined
                        anchors.right: undefined
                        anchors.verticalCenter: undefined
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    PropertyChanges {
                        target: container
                        anchors.topMargin: root.containerTopMargin
                    }

                },
                State {
                    name: "topRight"

                    AnchorChanges {
                        target: container
                        anchors.top: parent.top
                        anchors.bottom: undefined
                        anchors.left: undefined
                        anchors.right: parent.right
                        anchors.verticalCenter: undefined
                        anchors.horizontalCenter: undefined
                    }

                    PropertyChanges {
                        target: container
                        anchors.topMargin: root.containerTopMargin
                        anchors.rightMargin: root.containerSideMargin
                    }

                },
                State {
                    name: "left"

                    AnchorChanges {
                        target: container
                        anchors.top: undefined
                        anchors.bottom: undefined
                        anchors.left: parent.left
                        anchors.right: undefined
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenter: undefined
                    }

                    PropertyChanges {
                        target: container
                        anchors.leftMargin: root.containerSideMargin
                    }

                },
                State {
                    name: "center"

                    AnchorChanges {
                        target: container
                        anchors.top: undefined
                        anchors.bottom: undefined
                        anchors.left: undefined
                        anchors.right: undefined
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                },
                State {
                    name: "right"

                    AnchorChanges {
                        target: container
                        anchors.top: undefined
                        anchors.bottom: undefined
                        anchors.left: undefined
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenter: undefined
                    }

                    PropertyChanges {
                        target: container
                        anchors.rightMargin: root.containerSideMargin
                    }

                },
                State {
                    name: "bottomLeft"

                    AnchorChanges {
                        target: container
                        anchors.top: undefined
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: undefined
                        anchors.verticalCenter: undefined
                        anchors.horizontalCenter: undefined
                    }

                    PropertyChanges {
                        target: container
                        anchors.bottomMargin: root.containerTopMargin
                        anchors.leftMargin: root.containerSideMargin
                    }

                },
                State {
                    name: "bottom"

                    AnchorChanges {
                        target: container
                        anchors.top: undefined
                        anchors.bottom: parent.bottom
                        anchors.left: undefined
                        anchors.right: undefined
                        anchors.verticalCenter: undefined
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    PropertyChanges {
                        target: container
                        anchors.bottomMargin: root.containerTopMargin
                    }

                },
                State {
                    name: "bottomRight"

                    AnchorChanges {
                        target: container
                        anchors.top: undefined
                        anchors.bottom: parent.bottom
                        anchors.left: undefined
                        anchors.right: parent.right
                        anchors.verticalCenter: undefined
                        anchors.horizontalCenter: undefined
                    }

                    PropertyChanges {
                        target: container
                        anchors.bottomMargin: root.containerTopMargin
                        anchors.rightMargin: root.containerSideMargin
                    }

                }
            ]

            // ScrollView wrapper for overflow handling
            ScrollView {
                id: scrollView

                anchors.centerIn: parent
                width: root.contentWidth
                height: root.contentHeight
                clip: root.needsScrolling || root.needsHorizontalScrolling
                contentWidth: root.needsHorizontalScrolling ? root.scrollContentWidth : root.contentWidth
                contentHeight: root.needsScrolling ? root.scrollContentHeight : root.contentHeight
                // Only show scrollbars when needed
                ScrollBar.vertical.policy: root.needsScrolling ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                ScrollBar.horizontal.policy: root.needsHorizontalScrolling ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff

                // Layout previews grid
                GridLayout {
                    id: contentGrid

                    objectName: "zoneSelectorContentGrid"
                    width: root.needsHorizontalScrolling ? root.scrollContentWidth : root.contentWidth
                    height: root.needsScrolling ? root.scrollContentHeight : root.contentHeight
                    columns: root.layoutColumns
                    rowSpacing: root.indicatorSpacing
                    columnSpacing: root.indicatorSpacing

                    Repeater {
                        model: root.layouts

                        delegate: Item {
                            id: indicator

                            required property var modelData
                            required property int index
                            property string layoutId: modelData.id || ""
                            property bool isActive: layoutId === root.activeLayoutId
                            property bool hasSelectedZone: root.selectedLayoutId === layoutId

                            width: root.indicatorWidth + root.cardSidePadding * 2
                            height: root.indicatorHeight + root.labelSpace + root.cardPadding
                            Layout.preferredWidth: width
                            Layout.preferredHeight: height

                            QFZCommon.LayoutCard {
                                anchors.fill: parent
                                layoutData: indicator.modelData
                                isActive: indicator.isActive
                                isSelected: indicator.hasSelectedZone
                                globalAutoAssign: root.globalAutoAssign
                                previewWidth: root.indicatorWidth
                                previewHeight: root.indicatorHeight
                                // Zone selector features
                                showCardBackground: true
                                interactive: !(root.locked && !indicator.isActive)
                                selectedZoneIndex: indicator.hasSelectedZone ? root.selectedZoneIndex : -1
                                // Zone appearance
                                zonePadding: root.scaledPadding
                                edgeGap: root.scaledPadding
                                minZoneSize: root.minZoneSize
                                zoneHighlightColor: root.highlightColor
                                zoneInactiveColor: root.inactiveColor
                                zoneBorderColor: root.borderColor
                                inactiveOpacity: root.inactiveOpacity
                                activeOpacity: root.activeOpacity
                                hoverScale: 1.05
                                // Theme
                                highlightColor: root.highlightColor
                                textColor: root.textColor
                                backgroundColor: root.backgroundColor
                                // Font
                                fontFamily: root.fontFamily
                                fontSizeScale: root.fontSizeScale
                                fontWeight: root.fontWeight
                                fontItalic: root.fontItalic
                                fontUnderline: root.fontUnderline
                                fontStrikeout: root.fontStrikeout
                                // Animation
                                animationDuration: animationConstants.normalDuration
                                shortAnimationDuration: animationConstants.shortDuration
                                labelTopMargin: root.labelTopMargin
                                onZoneHovered: function(zoneIndex) {
                                    // Block zone interaction on locked non-active layouts
                                    if (root.locked && !indicator.isActive)
                                        return ;

                                    // Guard: don't re-emit when nothing changed.
                                    // C++ writes selectedLayoutId/selectedZoneIndex
                                    // back via property bindings when it hit-tests;
                                    // without this guard the round-trip would loop
                                    // through zoneSelected → C++ slot → property
                                    // write → onZoneHovered fires again.
                                    if (root.selectedLayoutId === indicator.layoutId && root.selectedZoneIndex === zoneIndex)
                                        return ;

                                    root.selectedLayoutId = indicator.layoutId;
                                    root.selectedZoneIndex = zoneIndex;
                                    var zones = indicator.modelData.zones || [];
                                    var zone = zones[zoneIndex];
                                    var relGeo = zone ? (zone.relativeGeometry || {
                                    }) : {
                                    };
                                    root.zoneSelected(indicator.layoutId, root.selectedZoneIndex, relGeo);
                                }
                            }

                            // Lock overlay for non-active layouts — absorbs all mouse events
                            Rectangle {
                                anchors.fill: parent
                                visible: root.locked && !indicator.isActive
                                z: 100
                                // Theme-derived scrim rather than raw black so
                                // light themes get a sensible dim colour too.
                                color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.5)
                                radius: Kirigami.Units.largeSpacing

                                Kirigami.Icon {
                                    anchors.centerIn: parent
                                    source: "object-locked"
                                    width: Math.min(parent.width, parent.height) * 0.3
                                    height: width
                                    color: Kirigami.Theme.highlightedTextColor
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.ForbiddenCursor
                                    Accessible.name: i18nc("@info:whatsthis zone selector lock overlay", "Layout is locked — switch to this layout before selecting a zone")
                                    onClicked: function(mouse) {
                                        mouse.accepted = true;
                                    }
                                    onPressed: function(mouse) {
                                        mouse.accepted = true;
                                    }
                                }

                            }

                        }

                    }

                }

            }

            // Top fade: visible when scrolled down (more content above)
            Rectangle {
                anchors.top: scrollView.top
                anchors.left: scrollView.left
                anchors.right: scrollView.right
                height: root.indicatorSpacing
                visible: root.needsScrolling && scrollView.ScrollBar.vertical.position > autoScrollConstants.scrollThreshold
                z: 10

                gradient: Gradient {
                    GradientStop {
                        position: 0
                        color: root.fadeColor
                    }

                    GradientStop {
                        position: 1
                        color: "transparent"
                    }

                }

            }

            // Bottom fade: visible when more content below
            Rectangle {
                anchors.bottom: scrollView.bottom
                anchors.left: scrollView.left
                anchors.right: scrollView.right
                height: root.indicatorSpacing
                visible: root.needsScrolling && (scrollView.ScrollBar.vertical.position + scrollView.ScrollBar.vertical.size) < (1 - autoScrollConstants.scrollThreshold)
                z: 10

                gradient: Gradient {
                    GradientStop {
                        position: 0
                        color: "transparent"
                    }

                    GradientStop {
                        position: 1
                        color: root.fadeColor
                    }

                }

            }

            // Left fade: visible when scrolled right (more content to the left)
            Rectangle {
                anchors.top: scrollView.top
                anchors.bottom: scrollView.bottom
                anchors.left: scrollView.left
                width: root.indicatorSpacing
                visible: root.needsHorizontalScrolling && scrollView.ScrollBar.horizontal.position > autoScrollConstants.scrollThreshold
                z: 10

                gradient: Gradient {
                    orientation: Gradient.Horizontal

                    GradientStop {
                        position: 0
                        color: root.fadeColor
                    }

                    GradientStop {
                        position: 1
                        color: "transparent"
                    }

                }

            }

            // Right fade: visible when more content to the right
            Rectangle {
                anchors.top: scrollView.top
                anchors.bottom: scrollView.bottom
                anchors.right: scrollView.right
                width: root.indicatorSpacing
                visible: root.needsHorizontalScrolling && (scrollView.ScrollBar.horizontal.position + scrollView.ScrollBar.horizontal.size) < (1 - autoScrollConstants.scrollThreshold)
                z: 10

                gradient: Gradient {
                    orientation: Gradient.Horizontal

                    GradientStop {
                        position: 0
                        color: "transparent"
                    }

                    GradientStop {
                        position: 1
                        color: root.fadeColor
                    }

                }

            }

            // Empty state
            Label {
                anchors.centerIn: parent
                text: i18nc("@info zone selector empty state", "No layouts available")
                color: Kirigami.Theme.disabledTextColor
                visible: root.layouts.length === 0
            }

        }

    }

}
