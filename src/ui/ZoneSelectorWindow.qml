// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Zone Selector Window - Overlay window showing layout previews
 * Appears when dragging window near screen edge
 * Styled similar to KZones for consistent KDE UX
 */
Window {
    // Whether ScrollView is needed
    // Border radius in pixels

    id: root

    // Layout data (array of layout objects with id, name, zones)
    property var layouts: []
    property string activeLayoutId: ""
    property string hoveredLayoutId: ""
    // Selected zone tracking
    property string selectedLayoutId: ""
    property int selectedZoneIndex: -1
    // Cursor position (updated from C++ during drag)
    property int cursorX: -1
    property int cursorY: -1
    // Screen aspect ratio (set from C++ based on actual screen)
    property real screenAspectRatio: 16 / 9
    property int screenWidth: 1920 // Actual screen width for scaling
    // Selector configuration from settings
    // Note: These are fallback defaults that match plasmazones.kcfg.
    // C++ (OverlayService) sets these properties with actual settings values.
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
    // Appearance properties - unified with ZoneOverlay/ZoneItem for consistent look
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
    property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
    property string fontFamily: ""
    property real fontSizeScale: 1.0
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

    // Animation constants
    QtObject {
        id: animationConstants
        readonly property int shortDuration: 150
        readonly property int normalDuration: 200
    }

    // Auto-scroll constants for drag-based scrolling
    // During window drag, the cursor is captured so scroll wheel events can't reach the ScrollView.
    // Instead, auto-scroll when cursor is near the edges of the scrollable area.
    QtObject {
        id: autoScrollConstants
        readonly property int edgeMargin: Kirigami.Units.gridUnit * 4 // ~32px trigger zone
        readonly property real maxSpeed: 0.04 // max scroll position change per tick (0-1 range)
        readonly property int interval: 16    // ~60fps
        readonly property real scrollThreshold: 0.01 // epsilon for scroll position comparisons
        readonly property real fadeOpacity: 0.95      // fade gradient edge opacity (matches container)
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
                    var topFactor = 1.0 - ly / autoScrollConstants.edgeMargin;
                    vBar.position = Math.max(0, vBar.position - autoScrollConstants.maxSpeed * topFactor);
                } else if (ly > scrollView.height - autoScrollConstants.edgeMargin && ly <= scrollView.height) {
                    // Near bottom edge — scroll down
                    var bottomFactor = 1.0 - (scrollView.height - ly) / autoScrollConstants.edgeMargin;
                    var maxPos = 1.0 - vBar.size;
                    vBar.position = Math.min(maxPos, vBar.position + autoScrollConstants.maxSpeed * bottomFactor);
                }
            }

            // Horizontal auto-scroll
            if (root.needsHorizontalScrolling) {
                var hBar = scrollView.ScrollBar.horizontal;
                if (lx >= 0 && lx < autoScrollConstants.edgeMargin) {
                    // Near left edge — scroll left
                    var leftFactor = 1.0 - lx / autoScrollConstants.edgeMargin;
                    hBar.position = Math.max(0, hBar.position - autoScrollConstants.maxSpeed * leftFactor);
                } else if (lx > scrollView.width - autoScrollConstants.edgeMargin && lx <= scrollView.width) {
                    // Near right edge — scroll right
                    var rightFactor = 1.0 - (scrollView.width - lx) / autoScrollConstants.edgeMargin;
                    var maxHPos = 1.0 - hBar.size;
                    hBar.position = Math.min(maxHPos, hBar.position + autoScrollConstants.maxSpeed * rightFactor);
                }
            }
        }
    }

    // Signals (zoneSelected is used by C++ for hover-based zone selection)
    signal zoneSelected(string layoutId, int zoneIndex, var relativeGeometry)

    // Window configuration for overlay - LayerShellQt handles layering on Wayland
    flags: Qt.FramelessWindowHint | Qt.Tool
    color: "transparent"

    // Main container - uses States for proper anchor management
    // Conditional anchors with undefined don't reliably unset in QML
    QFZCommon.PopupFrame {
        id: container

        objectName: "zoneSelectorContainer"
        width: root.containerWidth
        height: root.containerHeight
        backgroundColor: root.backgroundColor
        textColor: root.textColor
        containerRadius: root.containerRadius
        // State based on selectorPosition (0-8 grid, 4=center is disabled)
        // 0=TopLeft, 1=Top, 2=TopRight, 3=Left, 5=Right, 6=BottomLeft, 7=Bottom, 8=BottomRight
        state: {
            switch (selectorPosition) {
            case 0:
                return "topLeft";
            case 1:
                return "top";
            case 2:
                return "topRight";
            case 3:
                return "left";
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

                        width: root.indicatorWidth
                        height: root.indicatorHeight + root.labelSpace
                        Layout.preferredWidth: width
                        Layout.preferredHeight: height

                        QFZCommon.LayoutCard {
                            anchors.fill: parent
                            layoutData: indicator.modelData
                            isActive: indicator.isActive
                            isSelected: indicator.hasSelectedZone
                            previewWidth: root.indicatorWidth
                            previewHeight: root.indicatorHeight

                            // Zone selector features
                            showIndicatorBar: true
                            showCardBackground: false
                            interactive: true
                            selectedZoneIndex: indicator.hasSelectedZone ? root.selectedZoneIndex : -1

                            // Zone appearance
                            zonePadding: 1
                            edgeGap: 1
                            minZoneSize: 8
                            zoneHighlightColor: root.highlightColor
                            zoneInactiveColor: root.inactiveColor
                            zoneBorderColor: root.borderColor
                            inactiveOpacity: root.inactiveOpacity
                            activeOpacity: root.activeOpacity
                            hoverScale: 1.05

                            // Theme
                            highlightColor: root.highlightColor
                            textColor: root.textColor

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
                                root.selectedLayoutId = indicator.layoutId;
                                root.selectedZoneIndex = zoneIndex;
                                var zones = indicator.modelData.zones || [];
                                var zone = zones[zoneIndex];
                                var relGeo = zone ? (zone.relativeGeometry || {}) : {};
                                root.zoneSelected(indicator.layoutId, root.selectedZoneIndex, relGeo);
                            }
                        }
                    }

                }

            }

        }

        // Scroll fade indicators — show gradient edges when content overflows

        // Top fade: visible when scrolled down (more content above)
        Rectangle {
            anchors.top: scrollView.top
            anchors.left: scrollView.left
            anchors.right: scrollView.right
            height: root.indicatorSpacing
            visible: root.needsScrolling && scrollView.ScrollBar.vertical.position > autoScrollConstants.scrollThreshold
            z: 10
            gradient: Gradient {
                GradientStop { position: 0.0; color: root.fadeColor }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        // Bottom fade: visible when more content below
        Rectangle {
            anchors.bottom: scrollView.bottom
            anchors.left: scrollView.left
            anchors.right: scrollView.right
            height: root.indicatorSpacing
            visible: root.needsScrolling && (scrollView.ScrollBar.vertical.position + scrollView.ScrollBar.vertical.size) < (1.0 - autoScrollConstants.scrollThreshold)
            z: 10
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: root.fadeColor }
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
                GradientStop { position: 0.0; color: root.fadeColor }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        // Right fade: visible when more content to the right
        Rectangle {
            anchors.top: scrollView.top
            anchors.bottom: scrollView.bottom
            anchors.right: scrollView.right
            width: root.indicatorSpacing
            visible: root.needsHorizontalScrolling && (scrollView.ScrollBar.horizontal.position + scrollView.ScrollBar.horizontal.size) < (1.0 - autoScrollConstants.scrollThreshold)
            z: 10
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: root.fadeColor }
            }
        }

        // Empty state
        Label {
            anchors.centerIn: parent
            text: i18n("No layouts available")
            color: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.5)
            visible: root.layouts.length === 0
        }

    }

}
