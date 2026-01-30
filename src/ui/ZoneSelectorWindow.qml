// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
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
    property int selectorPosition: 0
    property int selectorLayoutMode: 1
    property int selectorGridColumns: 3
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
    property bool needsScrolling: false
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
    property color numberColor: Kirigami.Theme.textColor
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property real activeOpacity: 0.5 // Match Settings default
    property real inactiveOpacity: 0.3 // Match Settings default

    // Animation constants - avoid magic numbers per .cursorrules
    QtObject {
        id: animationConstants
        readonly property int shortDuration: 150
        readonly property int normalDuration: 200
        readonly property int longDuration: 300
    }

    // Signals (zoneSelected is used by C++ for hover-based zone selection)
    signal zoneSelected(string layoutId, int zoneIndex, var relativeGeometry)

    // Window configuration for overlay - LayerShellQt handles layering on Wayland
    flags: Qt.FramelessWindowHint | Qt.Tool
    color: "transparent"

    // Shadow effect for the container
    MultiEffect {
        source: container
        anchors.fill: container
        shadowEnabled: true
        shadowColor: Qt.rgba(0, 0, 0, 0.38)
        shadowBlur: 1
        shadowVerticalOffset: 2
        shadowHorizontalOffset: 0
    }

    // Main container - uses States for proper anchor management
    // Conditional anchors with undefined don't reliably unset in QML
    Rectangle {
        id: container

        objectName: "zoneSelectorContainer"
        width: root.containerWidth
        height: root.containerHeight
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
        color: Qt.rgba(backgroundColor.r, backgroundColor.g, backgroundColor.b, 0.95)
        radius: root.containerRadius
        border.color: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.2)
        border.width: 1
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
            clip: root.needsScrolling
            contentWidth: root.contentWidth
            contentHeight: root.needsScrolling ? root.scrollContentHeight : root.contentHeight
            // Only show scrollbar when needed
            ScrollBar.vertical.policy: root.needsScrolling ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            // Layout previews grid
            GridLayout {
                id: contentGrid

                objectName: "zoneSelectorContentGrid"
                width: root.contentWidth
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
                        property string layoutName: modelData.name || ("Layout " + (index + 1))
                        property var layoutZones: modelData.zones || []
                        property int layoutCategory: modelData.category !== undefined ? modelData.category : 0
                        property bool isAutotile: layoutCategory === 1
                        property bool isActive: layoutId === root.activeLayoutId
                        property bool hasSelectedZone: root.selectedLayoutId === layoutId

                        width: root.indicatorWidth
                        height: root.indicatorHeight + root.labelSpace // Preview height + label space
                        Layout.preferredWidth: width
                        Layout.preferredHeight: height

                        // Preview area (the actual layout preview) - contains zone rectangles
                        Item {
                            id: previewArea

                            width: root.indicatorWidth
                            height: root.indicatorHeight
                            anchors.top: parent.top

                            // Autotile: whole-preview MouseArea (in front, captures all hover/click)
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: indicator.isAutotile
                                z: 50
                                onEntered: {
                                    if (indicator.isAutotile) {
                                        root.selectedLayoutId = indicator.layoutId;
                                        root.selectedZoneIndex = 0;
                                        root.zoneSelected(indicator.layoutId, 0, {"x": 0, "y": 0, "width": 1, "height": 1});
                                    }
                                }
                            }

                            // Background for the indicator - shows active layout and hover states
                            Rectangle {
                                // Active layout gets a prominent highlight border
                                // Hovered layout gets a subtle border

                                anchors.fill: parent
                                // P1: Enhanced background tint for active layout
                                color: {
                                    if (indicator.isActive)
                                        return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.12);
                                    else if (indicator.hasSelectedZone)
                                        return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.08);
                                    return "transparent";
                                }
                                radius: Kirigami.Units.smallSpacing * 1.5
                                border.color: {
                                    if (indicator.isActive)
                                        return highlightColor;
                                    else if (indicator.hasSelectedZone)
                                        return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.3);
                                    return "transparent";
                                }
                                border.width: indicator.isActive ? 2 : 1

                                Behavior on color {
                                    ColorAnimation {
                                        duration: animationConstants.normalDuration
                                        easing.type: Easing.OutCubic
                                    }

                                }

                                Behavior on border.color {
                                    ColorAnimation {
                                        duration: animationConstants.normalDuration
                                        easing.type: Easing.OutCubic
                                    }

                                }

                                Behavior on border.width {
                                    NumberAnimation {
                                        duration: animationConstants.shortDuration
                                    }

                                }

                            }

                            // P1: Colored left border accent for active layout
                            Rectangle {
                                id: activeIndicatorBar

                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: -Math.round(Kirigami.Units.smallSpacing / 2)
                                anchors.topMargin: Kirigami.Units.smallSpacing
                                anchors.bottomMargin: Kirigami.Units.smallSpacing
                                width: indicator.isActive ? Kirigami.Units.smallSpacing : 0
                                radius: Math.round(Kirigami.Units.smallSpacing / 2)
                                color: Kirigami.Theme.highlightColor
                                opacity: indicator.isActive ? 1 : 0

                                Behavior on width {
                                    NumberAnimation {
                                        duration: animationConstants.normalDuration
                                        easing.type: Easing.OutCubic
                                    }

                                }

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: animationConstants.normalDuration
                                        easing.type: Easing.OutCubic
                                    }

                                }

                            }

                            // P1: Active layout checkmark badge - larger and more prominent
                            Rectangle {
                                id: activeBadge

                                readonly property int badgeSize: Math.round(Kirigami.Units.gridUnit * 2.5)

                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.rightMargin: Kirigami.Units.smallSpacing
                                anchors.topMargin: Kirigami.Units.smallSpacing
                                width: indicator.isActive ? badgeSize : 0
                                height: indicator.isActive ? badgeSize : 0
                                radius: badgeSize / 2
                                color: Kirigami.Theme.highlightColor
                                opacity: indicator.isActive ? 1 : 0
                                z: 100

                                Label {
                                    anchors.centerIn: parent
                                    text: "\u2713" // Checkmark unicode
                                    font.pixelSize: Math.round(Kirigami.Units.gridUnit * 1.5)
                                    font.bold: true
                                    color: Kirigami.Theme.highlightedTextColor
                                    visible: indicator.isActive
                                }

                                Behavior on width {
                                    NumberAnimation {
                                        duration: animationConstants.normalDuration
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.5
                                    }

                                }

                                Behavior on height {
                                    NumberAnimation {
                                        duration: animationConstants.normalDuration
                                        easing.type: Easing.OutBack
                                        easing.overshoot: 1.5
                                    }

                                }

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: animationConstants.shortDuration
                                    }

                                }

                            }

                            // Zone rectangles - now proper children of previewArea
                            Repeater {
                                model: indicator.layoutZones

                                delegate: Rectangle {
                                    id: zoneRect

                                    required property var modelData
                                    required property int index
                                    property var relGeo: modelData.relativeGeometry || {
                                    }
                                    // Autotile: all zones highlight together when layout selected
                                    property bool isZoneSelected: indicator.isAutotile
                                        ? (root.selectedLayoutId === indicator.layoutId && root.selectedZoneIndex >= 0)
                                        : (root.selectedLayoutId === indicator.layoutId && root.selectedZoneIndex === index)
                                    property bool isZoneHovered: indicator.isAutotile ? false : zoneMouseArea.containsMouse
                                    // Calculate actual pixel dimensions for tooltip
                                    property int actualWidth: Math.round((relGeo.width || 0.25) * root.screenWidth)
                                    property int actualHeight: Math.round((relGeo.height || 1) * root.screenWidth / root.screenAspectRatio)
                                    property int widthPercent: Math.round((relGeo.width || 0.25) * 100)
                                    property int heightPercent: Math.round((relGeo.height || 1) * 100)

                                    // DRY: Helper to check if custom colors are enabled (handles boolean, number, or string)
                                    readonly property bool useCustomColors: modelData.useCustomColors === true ||
                                        modelData.useCustomColors === 1 ||
                                        (typeof modelData.useCustomColors === "string" &&
                                         modelData.useCustomColors.toLowerCase() === "true")

                                    // Use scaled padding for gaps between zones
                                    // Position relative to parent (previewArea)
                                    x: (relGeo.x || 0) * parent.width + root.scaledPadding
                                    y: (relGeo.y || 0) * parent.height + root.scaledPadding
                                    // P0: Increase minimum zone size for easier targeting (3 gridUnits)
                                    width: Math.max(Kirigami.Units.gridUnit * 3, (relGeo.width || 0.25) * parent.width - root.scaledPadding * 2)
                                    height: Math.max(Kirigami.Units.gridUnit * 3, (relGeo.height || 1) * parent.height - root.scaledPadding * 2)
                                    radius: root.scaledBorderRadius
                                    // P0: Scale on hover for manual zones only; autotile highlights whole layout
                                    scale: isZoneHovered ? 1.05 : 1
                                    z: isZoneHovered ? 10 : 1
                                    transformOrigin: Item.Center
                                    // Zone coloring - unified with ZoneOverlay/ZoneItem
                                    // Use custom colors if enabled, otherwise use theme defaults
                                    color: {
                                        if (useCustomColors) {
                                            if (isZoneSelected && modelData.highlightColor)
                                                return modelData.highlightColor;
                                            else if (!isZoneSelected && modelData.inactiveColor)
                                                return modelData.inactiveColor;
                                        }
                                        // Fall back to theme colors
                                        return isZoneSelected ? highlightColor : inactiveColor;
                                    }
                                    opacity: {
                                        if (useCustomColors && modelData.activeOpacity !== undefined)
                                            return isZoneSelected ? modelData.activeOpacity : modelData.inactiveOpacity;

                                        return isZoneSelected ? activeOpacity : inactiveOpacity;
                                    }
                                    // Border - same style as ZoneItem (always visible)
                                    // P0: Brighter border on hover for better visibility
                                    border.color: {
                                        if (useCustomColors && modelData.borderColor)
                                            return modelData.borderColor;

                                        // Brighter border when hovered
                                        if (isZoneHovered)
                                            return Qt.rgba(Math.min(1, Kirigami.Theme.highlightColor.r * 1.2), Math.min(1, Kirigami.Theme.highlightColor.g * 1.2), Math.min(1, Kirigami.Theme.highlightColor.b * 1.2), 1);

                                        return borderColor;
                                    }
                                    border.width: isZoneHovered ? root.scaledBorderWidth + 1 : root.scaledBorderWidth

                                    // P1: Zone number label - more prominent with keyboard hint
                                    Label {
                                        id: zoneNumberLabel

                                        anchors.centerIn: parent
                                        text: (zoneRect.index + 1).toString()
                                        // P1: Larger, more prominent number labels
                                        font.pixelSize: Math.max(10, Math.min(parent.width, parent.height) * 0.4)
                                        font.bold: true
                                        color: numberColor
                                        opacity: zoneRect.isZoneSelected || zoneRect.isZoneHovered ? 1 : 0.8
                                        visible: parent.width >= 16 && parent.height >= 16
                                        // Style enhancement for keyboard shortcut hint
                                        style: (zoneRect.index < 9 && zoneRect.isZoneHovered) ? Text.Outline : Text.Normal
                                        styleColor: Qt.rgba(0, 0, 0, 0.3)

                                        Behavior on opacity {
                                            NumberAnimation {
                                                duration: animationConstants.shortDuration
                                                easing.type: Easing.OutCubic
                                            }

                                        }

                                        Behavior on font.pixelSize {
                                            NumberAnimation {
                                                duration: 100
                                            }

                                        }

                                    }

                                    // Mouse interaction for non-drag selection
                                    MouseArea {
                                        id: zoneMouseArea

                                        anchors.fill: parent
                                        anchors.margins: -2
                                        hoverEnabled: true
                                        onEntered: {
                                            root.selectedLayoutId = indicator.layoutId;
                                            // Autotile: whole layout selection (zone index 0 is sentinel)
                                            root.selectedZoneIndex = indicator.isAutotile ? 0 : zoneRect.index;
                                            root.zoneSelected(indicator.layoutId, root.selectedZoneIndex,
                                                             indicator.isAutotile ? {"x": 0, "y": 0, "width": 1, "height": 1} : zoneRect.relGeo);
                                        }
                                    }

                                    // P2: Smoother animations with easing curves
                                    Behavior on color {
                                        ColorAnimation {
                                            duration: animationConstants.normalDuration
                                            easing.type: Easing.OutCubic
                                        }

                                    }

                                    Behavior on opacity {
                                        NumberAnimation {
                                            duration: animationConstants.normalDuration
                                            easing.type: Easing.OutCubic
                                        }

                                    }

                                    Behavior on scale {
                                        NumberAnimation {
                                            duration: animationConstants.shortDuration
                                            easing.type: Easing.OutBack
                                            easing.overshoot: 1.2
                                        }

                                    }

                                    Behavior on border.color {
                                        ColorAnimation {
                                            duration: animationConstants.shortDuration
                                            easing.type: Easing.OutCubic
                                        }

                                    }

                                    Behavior on border.width {
                                        NumberAnimation {
                                            duration: animationConstants.shortDuration
                                            easing.type: Easing.OutCubic
                                        }

                                    }

                                }

                            }

                        }

                        // Layout name label with category badge below preview
                        Row {
                            id: nameLabelRow

                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: previewArea.bottom
                            anchors.topMargin: root.labelTopMargin
                            spacing: Kirigami.Units.smallSpacing

                            // Category badge (Manual/Auto) - inline with name
                            QFZCommon.CategoryBadge {
                                id: categoryBadge

                                anchors.verticalCenter: parent.verticalCenter
                                category: indicator.layoutCategory
                            }

                            Label {
                                id: nameLabel

                                anchors.verticalCenter: parent.verticalCenter
                                text: indicator.layoutName
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 1
                                // P1: More prominent styling for active layout label
                                font.weight: indicator.isActive ? Font.Bold : Font.Normal
                                color: {
                                    if (indicator.isActive)
                                        return Kirigami.Theme.highlightColor;
                                    else if (indicator.hasSelectedZone)
                                        return textColor;
                                    return Qt.rgba(textColor.r, textColor.g, textColor.b, 0.6);
                                }
                                opacity: indicator.hasSelectedZone || indicator.isActive ? 1 : 0.8

                                // P2: Smoother animations with easing
                                Behavior on color {
                                    ColorAnimation {
                                        duration: animationConstants.normalDuration
                                        easing.type: Easing.OutCubic
                                    }

                                }

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: animationConstants.normalDuration
                                        easing.type: Easing.OutCubic
                                    }

                                }

                            }

                        }

                    }

                }

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
