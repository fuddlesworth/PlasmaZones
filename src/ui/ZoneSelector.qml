// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * Zone Selector - Slides in from top when dragging window near screen edge
 * Similar to KZones implementation:
 * - Shows mini previews of available layouts
 * - Three states: hidden, near (partially visible), expanded (when hovering)
 * - Smooth 150ms animations
 */
Rectangle {
    // Distance for "near" state (partially visible)
    // Public API functions
    // Optionally hide after selection
    // hide()

    id: root

    property color activeColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, activeOpacity)
    // Array of layout objects with zones
    property string activeLayoutId: ""
    readonly property real activeOpacity: 0.7
    property color backgroundColor: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, backgroundOpacity)
    readonly property real backgroundOpacity: 0.95
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, borderOpacity)
    // Theme colors with opacity constants
    readonly property real borderOpacity: 0.6
    property int containerPadding: Kirigami.Units.gridUnit * 1.5 // 12px
    // "hidden", "near", "expanded"
    property real cursorProximity: 1
    property real expandedY: Kirigami.Units.largeSpacing * 2 // 16px from top
    // Font properties for zone number labels
    property string fontFamily: ""
    property bool fontItalic: false
    property real fontSizeScale: 1
    property bool fontStrikeout: false
    property bool fontUnderline: false
    property int fontWeight: Font.Bold
    // Position calculation based on state
    property real hiddenY: -height - Kirigami.Units.gridUnit
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, highlightOpacity)
    readonly property real highlightOpacity: 0.5
    readonly property real hoverOpacity: 0.4
    property string hoveredLayoutId: ""
    readonly property real inactiveOpacity: 0.2
    property int labelHeight: Kirigami.Units.gridUnit * 2 // Space for layout name labels
    // Layout data
    property var layouts: []
    // Preview dimensions
    property bool locked: false
    // Distance from top edge to show selector
    property int nearDistance: 50
    property real nearY: -height + Kirigami.Units.gridUnit * 2 // Peek 16px
    property int previewHeight: 70
    property int previewSpacing: Kirigami.Units.gridUnit // 8px
    property int previewWidth: 130
    // State management
    property string selectorState: "hidden"
    readonly property real textSecondaryOpacity: 0.6
    // 0.0 = at edge, 1.0 = far away
    property int triggerDistance: 100

    signal layoutHovered(string layoutId)
    // Signals
    signal layoutSelected(string layoutId)
    signal selectorStateEdited(string newState)

    /**
     * Expand the selector fully
     */
    function expand() {
        setState("expanded");
    }

    /**
     * Hide the selector completely
     */
    function hide() {
        setState("hidden");
    }

    /**
     * Handle layout selection
     */
    function selectLayout(layoutId) {
        if (root.locked)
            return;

        activeLayoutId = layoutId;
        layoutSelected(layoutId);
    }

    /**
     * Set the active layout by ID
     */
    function setActiveLayout(layoutId) {
        activeLayoutId = layoutId;
    }

    /**
     * Set available layouts
     */
    function setLayouts(layoutList) {
        layouts = layoutList;
    }

    /**
     * Set the selector state
     * @param newState - "hidden", "near", or "expanded"
     */
    function setState(newState) {
        if (selectorState !== newState) {
            selectorState = newState;
            selectorStateEdited(newState);
        }
    }

    /**
     * Show the selector (transition to near state)
     */
    function show() {
        if (selectorState === "hidden")
            setState("near");
    }

    /**
     * Toggle between states
     */
    function toggle() {
        switch (selectorState) {
        case "hidden":
            setState("near");
            break;
        case "near":
            setState("expanded");
            break;
        case "expanded":
            setState("hidden");
            break;
        }
    }

    /**
     * Update cursor proximity (0.0 = at edge, 1.0 = far away)
     * This controls the automatic state transitions
     */
    function updateProximity(proximity) {
        cursorProximity = Math.max(0, Math.min(1, proximity));
        if (cursorProximity < 0.3 && selectorState === "hidden")
            setState("near");
        else if (cursorProximity >= 0.7 && selectorState !== "hidden")
            setState("hidden");
    }

    // Center horizontally
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    border.color: borderColor
    border.width: constants.standardBorderWidth
    // Visual appearance
    color: backgroundColor
    implicitHeight: previewHeight + containerPadding * 2 + labelHeight
    // Calculate dimensions based on content
    implicitWidth: Math.min(contentRow.width + containerPadding * 2, parent ? parent.width - Kirigami.Units.gridUnit * 4 : 800)
    // Opacity based on state
    opacity: selectorState === "hidden" ? 0 : 1
    radius: Kirigami.Units.gridUnit * 1.5 // 12px
    // Computed y position based on state
    y: {
        switch (selectorState) {
        case "hidden":
            return hiddenY;
        case "near":
            return nearY;
        case "expanded":
            return expandedY;
        default:
            return hiddenY;
        }
    }

    // Constants for visual styling
    QtObject {
        id: constants

        readonly property int animationDuration: Kirigami.Units.shortDuration
        readonly property int expandedAnimationDuration: Kirigami.Units.longDuration
        readonly property int standardBorderWidth: Kirigami.Units.smallSpacing / 2 // 2px
        readonly property int thickBorderWidth: Kirigami.Units.smallSpacing // 4px
        readonly property int thinBorderWidth: 1
    }

    // Subtle glow/shadow effect
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, root.selectorState === "expanded" ? 0.3 : 0.1)
        border.width: constants.thickBorderWidth
        color: "transparent"
        radius: parent.radius + 3
        z: -1

        Behavior on border.color {
            PhosphorMotionAnimation {
                durationOverride: constants.animationDuration
                profile: "popup"
            }
        }
    }

    // Header label when expanded
    Label {
        id: headerLabel

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Kirigami.Units.smallSpacing
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, textSecondaryOpacity)
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
        font.weight: Font.Medium
        opacity: root.selectorState === "expanded" ? 1 : 0
        text: i18n("Select Layout")
        visible: opacity > 0

        Behavior on opacity {
            PhosphorMotionAnimation {
                durationOverride: constants.animationDuration
                profile: "popup"
            }
        }
    }

    // Layout previews container
    Flickable {
        id: flickable

        anchors.fill: parent
        anchors.margins: root.containerPadding
        anchors.topMargin: root.selectorState === "expanded" ? root.containerPadding + headerLabel.height + Kirigami.Units.smallSpacing : root.containerPadding
        clip: true
        contentHeight: contentRow.height
        contentWidth: contentRow.width
        interactive: contentWidth > width

        Row {
            id: contentRow

            spacing: root.previewSpacing

            Repeater {
                model: root.layouts

                delegate: LayoutPreview {
                    required property int index
                    required property var modelData

                    activeColor: root.activeColor
                    borderColor: root.borderColor
                    category: modelData.category !== undefined ? modelData.category : 0
                    fontFamily: root.fontFamily
                    fontItalic: root.fontItalic
                    fontSizeScale: root.fontSizeScale
                    fontStrikeout: root.fontStrikeout
                    fontUnderline: root.fontUnderline
                    fontWeight: root.fontWeight
                    highlightColor: root.highlightColor
                    hoverOpacity: root.hoverOpacity
                    inactiveOpacity: root.inactiveOpacity
                    isActive: layoutId === root.activeLayoutId
                    isHovered: layoutId === root.hoveredLayoutId
                    layoutId: modelData.id || ""
                    layoutName: modelData.name || i18n("Layout %1", index + 1)
                    // Show lock overlay on non-active layouts when screen is locked
                    locked: root.locked && !isActive
                    previewHeight: root.previewHeight
                    previewWidth: root.previewWidth
                    producesOverlappingZones: modelData.producesOverlappingZones === true
                    showMasterDot: modelData.isAutotile === true && modelData.supportsMasterCount === true
                    zoneNumberDisplay: modelData.zoneNumberDisplay || "all"
                    zones: modelData.zones || []
                    onClicked: {
                        root.selectLayout(layoutId);
                    }
                    onHovered: {
                        if (root.locked)
                            return;

                        root.hoveredLayoutId = layoutId;
                        root.layoutHovered(layoutId);
                    }
                    onUnhovered: {
                        if (root.hoveredLayoutId === layoutId)
                            root.hoveredLayoutId = "";
                    }
                }
            }
        }
    }

    // Mouse area to expand when hovering
    MouseArea {
        Accessible.name: i18n("Zone layout selector")
        anchors.fill: parent
        hoverEnabled: true
        propagateComposedEvents: true
        onEntered: {
            if (root.selectorState === "near")
                root.setState("expanded");
        }
        onExited: {
            // Delay collapse to allow selecting layouts
            collapseTimer.restart();
        }
        onPressed: function (mouse) {
            mouse.accepted = false; // Let child items handle clicks
        }
    }

    // Timer to delay collapse after mouse leaves
    Timer {
        id: collapseTimer

        function isMouseOver() {
            // Check if mouse is still over the selector
            var mousePos = root.mapFromGlobal(Qt.point(0, 0));
            return root.contains(mousePos);
        }

        interval: 300 // ms delay before collapsing
        onTriggered: {
            if (root.selectorState === "expanded" && !isMouseOver())
                root.setState("near");
        }
    }

    // Empty state message
    Label {
        anchors.centerIn: parent
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, textSecondaryOpacity)
        font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
        text: i18n("No layouts available")
        visible: root.layouts.length === 0 && root.selectorState !== "hidden"
    }

    Behavior on opacity {
        PhosphorMotionAnimation {
            durationOverride: constants.animationDuration
            profile: "popup"
        }
    }

    // Smooth slide animation. durationOverride preserves the original
    // dynamic-duration logic — expanded mode slides slightly slower.
    Behavior on y {
        PhosphorMotionAnimation {
            durationOverride: root.selectorState === "expanded" ? constants.expandedAnimationDuration : constants.animationDuration
            profile: "popup"
        }
    }
}
