// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

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

    // Layout data
    property var layouts: []
    // Array of layout objects with zones
    property string activeLayoutId: ""
    property string hoveredLayoutId: ""
    // State management
    property string selectorState: "hidden"
    // "hidden", "near", "expanded"
    property real cursorProximity: 1
    // 0.0 = at edge, 1.0 = far away
    property int triggerDistance: 100
    // Distance from top edge to show selector
    property int nearDistance: 50
    // Theme colors with opacity constants
    readonly property real borderOpacity: 0.6
    readonly property real highlightOpacity: 0.5
    readonly property real activeOpacity: 0.7
    readonly property real inactiveOpacity: 0.2
    readonly property real hoverOpacity: 0.4
    readonly property real textSecondaryOpacity: 0.6
    readonly property real backgroundOpacity: 0.95
    property color backgroundColor: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, backgroundOpacity)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, borderOpacity)
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, highlightOpacity)
    property color activeColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, activeOpacity)
    // Preview dimensions
    property int previewWidth: 130
    property int previewHeight: 70
    property int previewSpacing: Kirigami.Units.gridUnit // 8px
    property int containerPadding: Kirigami.Units.gridUnit * 1.5 // 12px
    property int labelHeight: Kirigami.Units.gridUnit * 2 // Space for layout name labels
    // Position calculation based on state
    property real hiddenY: -height - Kirigami.Units.gridUnit
    property real nearY: -height + Kirigami.Units.gridUnit * 2 // Peek 16px
    property real expandedY: Kirigami.Units.largeSpacing * 2 // 16px from top

    // Signals
    signal layoutSelected(string layoutId)
    signal layoutHovered(string layoutId)
    signal selectorStateChanged(string newState)

    /**
     * Set the selector state
     * @param newState - "hidden", "near", or "expanded"
     */
    function setState(newState) {
        if (selectorState !== newState) {
            selectorState = newState;
            selectorStateChanged(newState);
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

    /**
     * Show the selector (transition to near state)
     */
    function show() {
        if (selectorState === "hidden")
            setState("near");

    }

    /**
     * Hide the selector completely
     */
    function hide() {
        setState("hidden");
    }

    /**
     * Expand the selector fully
     */
    function expand() {
        setState("expanded");
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
     * Set available layouts
     */
    function setLayouts(layoutList) {
        layouts = layoutList;
    }

    /**
     * Set the active layout by ID
     */
    function setActiveLayout(layoutId) {
        activeLayoutId = layoutId;
    }

    /**
     * Handle layout selection
     */
    function selectLayout(layoutId) {
        activeLayoutId = layoutId;
        layoutSelected(layoutId);
    }

    // Calculate dimensions based on content
    implicitWidth: Math.min(contentRow.width + containerPadding * 2, parent ? parent.width - Kirigami.Units.gridUnit * 4 : 800)
    implicitHeight: previewHeight + containerPadding * 2 + labelHeight
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
    // Center horizontally
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    // Visual appearance
    color: backgroundColor
    radius: Kirigami.Units.gridUnit * 1.5 // 12px
    border.color: borderColor
    border.width: constants.standardBorderWidth
    // Opacity based on state
    opacity: selectorState === "hidden" ? 0 : 1

    // Constants for visual styling
    QtObject {
        id: constants

        readonly property int standardBorderWidth: Kirigami.Units.smallSpacing / 2 // 2px
        readonly property int thickBorderWidth: Kirigami.Units.smallSpacing // 4px
        readonly property int thinBorderWidth: 1
        readonly property int animationDuration: 150 // ms - consistent timing
        readonly property int expandedAnimationDuration: 200 // ms - slightly longer for expand
    }

    // Subtle glow/shadow effect
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        z: -1
        color: "transparent"
        radius: parent.radius + 3
        border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, root.selectorState === "expanded" ? 0.3 : 0.1)
        border.width: constants.thickBorderWidth

        Behavior on border.color {
            ColorAnimation {
                duration: constants.animationDuration
            }

        }

    }

    // Header label when expanded
    Label {
        id: headerLabel

        anchors.top: parent.top
        anchors.topMargin: Kirigami.Units.smallSpacing
        anchors.horizontalCenter: parent.horizontalCenter
        text: i18n("Select Layout")
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
        font.weight: Font.Medium
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, textSecondaryOpacity)
        opacity: root.selectorState === "expanded" ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation {
                duration: constants.animationDuration
            }

        }

    }

    // Layout previews container
    Flickable {
        id: flickable

        anchors.fill: parent
        anchors.margins: root.containerPadding
        anchors.topMargin: root.selectorState === "expanded" ? root.containerPadding + headerLabel.height + Kirigami.Units.smallSpacing : root.containerPadding
        contentWidth: contentRow.width
        contentHeight: contentRow.height
        clip: true
        interactive: contentWidth > width

        Row {
            id: contentRow

            spacing: root.previewSpacing

            Repeater {
                model: root.layouts

                delegate: LayoutPreview {
                    required property var modelData
                    required property int index

                    layoutId: modelData.id || ""
                    layoutName: modelData.name || i18n("Layout %1", index + 1)
                    zones: modelData.zones || []
                    category: modelData.category !== undefined ? modelData.category : 0
                    isActive: layoutId === root.activeLayoutId
                    isHovered: layoutId === root.hoveredLayoutId
                    previewWidth: root.previewWidth
                    previewHeight: root.previewHeight
                    highlightColor: root.highlightColor
                    activeColor: root.activeColor
                    borderColor: root.borderColor
                    inactiveOpacity: root.inactiveOpacity
                    hoverOpacity: root.hoverOpacity
                    onClicked: {
                        root.layoutSelected(layoutId);
                    }
                    onHovered: {
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
        onPressed: function(mouse) {
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
        text: i18n("No layouts available")
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, textSecondaryOpacity)
        font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
        visible: root.layouts.length === 0 && root.selectorState !== "hidden"
    }

    // Smooth slide animation
    Behavior on y {
        NumberAnimation {
            duration: selectorState === "expanded" ? constants.expandedAnimationDuration : constants.animationDuration
            easing.type: Easing.OutQuad
        }

    }

    Behavior on opacity {
        NumberAnimation {
            duration: constants.animationDuration
        }

    }

}
