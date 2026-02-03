// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * Navigation OSD Window - Shows brief feedback when using keyboard navigation
 * to move or focus windows between zones
 * Auto-dismisses after ~1 second
 */
Window {
    id: root

    // Note: Accessible properties moved to container (Window doesn't support Accessible)

    // Navigation feedback data
    property bool success: true
    property string action: "" // "move", "focus", "push", "restore", "float", "swap", "rotate", "snap", "cycle"
    property string reason: "" // Failure reason if !success, direction for rotation (clockwise/counterclockwise), or float state (floated/unfloated)

    // Zone data
    property var zones: []
    property var highlightedZoneIds: [] // Zone IDs involved (target zones)
    property string sourceZoneId: ""    // Source zone for move/swap operations
    property int windowCount: 1         // Number of windows affected (for rotation)

    // Timing
    property int displayDuration: 1000 // ms before auto-hide (shorter than layout OSD)
    property int fadeInDuration: 150
    property int fadeOutDuration: 200

    // Theme colors
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightColor: Kirigami.Theme.highlightColor
    property color errorColor: Kirigami.Theme.negativeTextColor

    // Helper function to normalize UUID format for comparison
    // Handles both "{uuid}" and "uuid" formats by stripping braces
    function normalizeUuid(uuid) {
        if (!uuid) return "";
        var s = String(uuid);
        // Remove leading/trailing braces if present
        if (s.startsWith("{") && s.endsWith("}")) {
            return s.substring(1, s.length - 1).toLowerCase();
        }
        return s.toLowerCase();
    }

    // Helper function to get zone number from zone ID
    function getZoneNumber(zoneId) {
        if (!zoneId || !zones || zones.length === 0) return -1;
        var normalizedTarget = normalizeUuid(zoneId);
        for (var i = 0; i < zones.length; i++) {
            var zone = zones[i];
            var id = zone.zoneId || zone.id || "";
            // Compare normalized UUIDs to handle format differences
            if (normalizeUuid(id) === normalizedTarget) {
                return zone.zoneNumber !== undefined ? zone.zoneNumber : (i + 1);
            }
        }
        return -1;
    }

    // Get target zone number (first highlighted zone)
    readonly property int targetZoneNumber: {
        if (highlightedZoneIds && highlightedZoneIds.length > 0) {
            return getZoneNumber(highlightedZoneIds[0]);
        }
        return -1;
    }

    // Get source zone number
    readonly property int sourceZoneNumber: getZoneNumber(sourceZoneId)

    // Computed message text - informative zone-based messages
    readonly property string messageText: {
        if (!success) {
            // Failure messages
            if (action === "move" || action === "focus") {
                return i18n("No zone in that direction")
            } else if (action === "push") {
                return i18n("No empty zone available")
            } else if (action === "rotate") {
                return i18n("Nothing to rotate")
            } else if (action === "swap") {
                return i18n("Nothing to swap")
            } else {
                return i18n("Failed")
            }
        }

        // Success messages with zone numbers
        if (action === "rotate") {
            var arrow = (reason === "clockwise") ? "↻" : "↺";
            if (windowCount > 1) {
                return arrow + " " + i18np("Rotated %1 window", "Rotated %1 windows", windowCount);
            } else {
                return arrow + " " + i18n("Rotated");
            }
        } else if (action === "move") {
            if (targetZoneNumber > 0) {
                return i18n("→ Zone %1", targetZoneNumber);
            }
            return i18n("Moved");
        } else if (action === "focus") {
            if (targetZoneNumber > 0) {
                return i18n("Focus: Zone %1", targetZoneNumber);
            }
            return i18n("Focus");
        } else if (action === "swap") {
            if (sourceZoneNumber > 0 && targetZoneNumber > 0) {
                return i18n("Zone %1 ↔ Zone %2", sourceZoneNumber, targetZoneNumber);
            }
            return i18n("Swapped");
        } else if (action === "push") {
            if (targetZoneNumber > 0) {
                return i18n("→ Zone %1", targetZoneNumber);
            }
            return i18n("Pushed");
        } else if (action === "restore") {
            return i18n("Restored");
        } else if (action === "float") {
            // Show different message based on float state from reason field
            if (reason === "unfloated") {
                return i18n("Snapped");
            }
            return i18n("Floating");
        } else if (action === "snap") {
            if (targetZoneNumber > 0) {
                return i18n("Snapped: Zone %1", targetZoneNumber);
            }
            return i18n("Snapped");
        } else if (action === "cycle") {
            return i18n("Next window");
        } else if (action === "resnap") {
            if (windowCount > 1) {
                return i18np("Resnapped %1 window", "Resnapped %1 windows", windowCount);
            }
            return i18n("Resnapped");
        } else {
            return i18n("Done");
        }
    }

    // Signals
    signal dismissed()

    // Window configuration - LayerShellQt handles overlay behavior on Wayland
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"

    // Size based on container (which is inside contentWrapper)
    width: container.width + 40
    height: container.height + 40

    // Start hidden, will be shown with animation
    // Note: Don't set Window.opacity - use contentWrapper.opacity instead
    // QWaylandWindow::setOpacity() is not implemented and logs warnings
    visible: false

    // Show the OSD with animation
    function show() {
        // Stop any running animations to prevent conflicts
        showAnimation.stop();
        hideAnimation.stop();
        dismissTimer.stop();

        // Reset state for fresh animation (animate wrapper, not window)
        contentWrapper.opacity = 0;
        container.scale = 0.8;

        root.visible = true;
        showAnimation.start();
        dismissTimer.restart();
    }

    // Hide the OSD with animation
    function hide() {
        // Stop show animation if running
        showAnimation.stop();
        dismissTimer.stop();

        // Only hide if visible
        if (root.visible) {
            hideAnimation.start();
        }
    }

    // Auto-dismiss timer
    Timer {
        id: dismissTimer

        interval: root.displayDuration
        onTriggered: root.hide()
    }

    // Show animation
    ParallelAnimation {
        id: showAnimation

        NumberAnimation {
            target: contentWrapper
            property: "opacity"
            from: 0
            to: 1
            duration: root.fadeInDuration
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: container
            property: "scale"
            from: 0.8
            to: 1
            duration: root.fadeInDuration
            easing.type: Easing.OutBack
            easing.overshoot: 1.2
        }

    }

    // Hide animation
    SequentialAnimation {
        id: hideAnimation

        ParallelAnimation {
            NumberAnimation {
                target: contentWrapper
                property: "opacity"
                to: 0
                duration: root.fadeOutDuration
                easing.type: Easing.InCubic
            }

            NumberAnimation {
                target: container
                property: "scale"
                to: 0.9
                duration: root.fadeOutDuration
                easing.type: Easing.InCubic
            }

        }

        ScriptAction {
            script: {
                root.visible = false;
                root.dismissed();
            }
        }

    }

    // Content wrapper - animates opacity instead of Window
    // This avoids "This plugin does not support setting window opacity" on Wayland
    Item {
        id: contentWrapper

        anchors.fill: parent
        opacity: 0

        // Shadow effect
        MultiEffect {
            source: container
            anchors.fill: container
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.5)
            shadowBlur: 1.0
            shadowVerticalOffset: 4
            shadowHorizontalOffset: 0
        }

        // Main container - matches LayoutOsd format exactly
        Rectangle {
            id: container

            Accessible.name: i18n("Navigation feedback")
            Accessible.description: i18n("Brief feedback when using keyboard navigation to move or focus windows between zones")

            anchors.centerIn: parent
            // Text-only: size based on message content
            width: Math.max(messageLabel.implicitWidth + Kirigami.Units.gridUnit * 3, 160)
            height: messageLabel.implicitHeight + Kirigami.Units.gridUnit * 2.5
        color: Qt.rgba(backgroundColor.r, backgroundColor.g, backgroundColor.b, 0.95)
        radius: Kirigami.Units.gridUnit * 1.5
        border.color: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.15)
        border.width: 1

        // Message label - informative text-based feedback
        Label {
            id: messageLabel

            Accessible.name: root.messageText

            anchors.top: parent.top
            anchors.topMargin: Kirigami.Units.gridUnit * 1.5
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: Kirigami.Units.gridUnit * 1.5
            text: root.messageText
            font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.3
            font.weight: Font.Medium
            color: root.success ? textColor : errorColor
            horizontalAlignment: Text.AlignHCenter
        }

    }

        // Click to dismiss
        MouseArea {
            anchors.fill: parent
            onClicked: root.hide()
        }

    } // contentWrapper

}
