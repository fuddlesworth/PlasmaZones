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
    // Note: Accessible properties moved to container (Window doesn't support Accessible)
    // contentWrapper
    // (No signals — see matching comment in LayoutOsd.qml. The dismiss
    // mechanism is the _osdDismissed flip in hideAnimation's ScriptAction.)

    id: root

    // Navigation feedback data
    property bool success: true
    property string action: "" // "move", "focus", "push", "restore", "float", "swap", "rotate", "snap", "cycle", "algorithm"
    property string reason: "" // Failure reason if !success, direction for rotation (clockwise/counterclockwise), or float state (floated/unfloated)
    // Zone data
    property var zones: []
    property var highlightedZoneIds: [] // Zone IDs involved (target zones)
    property string sourceZoneId: "" // Source zone for move/swap operations
    property int windowCount: 1 // Number of windows affected (for rotation)
    // Timing
    property int displayDuration: 1000
    // ms before auto-hide (shorter than layout OSD)
    property int fadeInDuration: 150
    property int fadeOutDuration: 200
    // Theme colors
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightColor: Kirigami.Theme.highlightColor
    property color errorColor: Kirigami.Theme.negativeTextColor
    // Get target zone number (first highlighted zone)
    readonly property int targetZoneNumber: {
        if (highlightedZoneIds && highlightedZoneIds.length > 0)
            return getZoneNumber(highlightedZoneIds[0]);

        return -1;
    }
    // Get source zone number
    readonly property int sourceZoneNumber: getZoneNumber(sourceZoneId)
    // Computed message text - informative zone-based messages
    readonly property string messageText: {
        if (!success) {
            // Failure messages
            if (action === "move" || action === "focus")
                return i18n("No zone in that direction");
            else if (action === "push")
                return i18n("No empty zone available");
            else if (action === "rotate")
                return i18n("Nothing to rotate");
            else if (action === "swap")
                return i18n("Nothing to swap");
            else if (action === "focus_master")
                return i18n("No windows to focus");
            else if (action === "swap_master")
                return reason === "already_master" ? i18n("Already in main position") : i18n("Nothing to swap");
            else
                return i18n("Failed");
        }
        // Success messages with zone numbers
        if (action === "rotate") {
            var arrow = (reason === "clockwise") ? "↻" : "↺";
            if (windowCount > 1)
                return arrow + " " + i18np("Rotated %n window", "Rotated %n windows", windowCount);
            else
                return arrow + " " + i18n("Rotated");
        } else if (action === "move") {
            var moveArrow = directionArrow(reason);
            if (targetZoneNumber > 0)
                return moveArrow + " " + i18n("Zone %1", targetZoneNumber);

            return moveArrow + " " + i18n("Moved");
        } else if (action === "focus") {
            var focusArrow = directionArrow(reason);
            if (targetZoneNumber > 0)
                return focusArrow + " " + i18n("Focus: Zone %1", targetZoneNumber);

            return focusArrow + " " + i18n("Focus");
        } else if (action === "swap") {
            var swapArrow = directionArrow(reason);
            if (sourceZoneNumber > 0 && targetZoneNumber > 0)
                return swapArrow + " " + i18n("Zone %1 ↔ Zone %2", sourceZoneNumber, targetZoneNumber);

            return swapArrow + " " + i18n("Swapped");
        } else if (action === "push") {
            if (targetZoneNumber > 0)
                return i18n("→ Zone %1", targetZoneNumber);

            return i18n("Window pushed");
        } else if (action === "restore") {
            return i18n("Restored");
        } else if (action === "float") {
            // Show different message based on float state from reason field
            if (reason === "tiled")
                return i18n("Tiled");

            if (reason === "unfloated")
                return i18n("Snapped");

            return i18n("Floating");
        } else if (action === "snap") {
            if (targetZoneNumber > 0)
                return i18n("Snapped: Zone %1", targetZoneNumber);

            return i18n("Snapped");
        } else if (action === "cycle") {
            return i18n("Next window");
        } else if (action === "focus_master") {
            return i18n("Focus main window");
        } else if (action === "swap_master") {
            return i18n("Swapped with main window");
        } else if (action === "master_ratio") {
            // reason format: "increased:65" or "decreased:60"
            let parts = reason.split(":");
            let pct = parts.length >= 2 ? parts[1] : "";
            return pct ? i18n("Master ratio → %1%", pct) : i18n("Master ratio changed");
        } else if (action === "master_count") {
            let parts = reason.split(":");
            let count = parts.length >= 2 ? parts[1] : "";
            return count ? i18n("Master count → %1", count) : i18n("Master count changed");
        } else if (action === "retile") {
            return i18n("Layout refreshed");
        } else if (action === "resnap") {
            if (windowCount > 1)
                return i18np("Rearranged %n window", "Rearranged %n windows", windowCount);

            return i18n("Windows rearranged");
        } else if (action === "algorithm") {
            return i18n("Autotile: %1", reason || "");
        } else {
            return i18n("Action completed");
        }
    }
    // Dismiss state used for input gating — see identical comment in
    // LayoutOsd.qml for the full rationale. Short version: we never set
    // root.visible = false after first show (Qt Vulkan + Wayland layer-shell
    // can't reliably reinit the swap chain on reshow), so an otherwise
    // invisible-but-Qt-visible window would eat clicks at its screen position.
    // Toggling Qt.WindowTransparentForInput via this boolean avoids that.
    property bool _osdDismissed: true

    // Helper function to normalize UUID format for comparison
    // Handles both "{uuid}" and "uuid" formats by stripping braces
    function normalizeUuid(uuid) {
        if (!uuid)
            return "";

        var s = String(uuid);
        // Remove leading/trailing braces if present
        if (s.startsWith("{") && s.endsWith("}"))
            return s.substring(1, s.length - 1).toLowerCase();

        return s.toLowerCase();
    }

    // Helper function to get zone number from zone ID
    function getZoneNumber(zoneId) {
        if (!zoneId || !zones || zones.length === 0)
            return -1;

        var normalizedTarget = normalizeUuid(zoneId);
        for (var i = 0; i < zones.length; i++) {
            var zone = zones[i];
            var id = zone.zoneId || zone.id || "";
            // Compare normalized UUIDs to handle format differences
            if (normalizeUuid(id) === normalizedTarget)
                return zone.zoneNumber !== undefined ? zone.zoneNumber : (i + 1);

        }
        return -1;
    }

    // Helper: direction string ("left","right","up","down") → arrow character
    function directionArrow(dir) {
        switch (dir) {
        case "left":
            return "←";
        case "right":
            return "→";
        case "up":
            return "↑";
        case "down":
            return "↓";
        default:
            return "→";
        }
    }

    // Show the OSD with animation
    function show() {
        // Stop any running animations to prevent conflicts
        showAnimation.stop();
        hideAnimation.stop();
        dismissTimer.stop();
        // Reset state for fresh animation (animate wrapper, not window)
        contentWrapper.opacity = 0;
        container.scale = 0.8;
        root._osdDismissed = false;
        root.visible = true;
        showAnimation.start();
        dismissTimer.restart();
    }

    // Hide the OSD with animation — see matching comment in LayoutOsd.qml.
    function hide() {
        if (root._osdDismissed)
            return ;

        showAnimation.stop();
        dismissTimer.stop();
        hideAnimation.start();
    }

    // Window configuration — see identical comment in LayoutOsd.qml. We keep
    // root.visible == true after the first show for the window's lifetime
    // and toggle Qt.WindowTransparentForInput via _osdDismissed to release
    // the input region when the OSD is visually gone.
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | (root._osdDismissed ? Qt.WindowTransparentForInput : 0)
    color: "transparent"
    // Size based on container (which is inside contentWrapper)
    width: container.width + Math.round(Kirigami.Units.gridUnit * 2.5)
    height: container.height + Math.round(Kirigami.Units.gridUnit * 2.5)
    // Start hidden, will be shown with animation
    // Note: Don't set Window.opacity - use contentWrapper.opacity instead
    // QWaylandWindow::setOpacity() is not implemented and logs warnings
    visible: false

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
                // Do NOT set root.visible = false — see LayoutOsd.qml for
                // the full rationale. Flip _osdDismissed so the window
                // flags binding engages Qt.WindowTransparentForInput.
                root._osdDismissed = true;
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
            shadowBlur: 1
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

        // Click to dismiss. Gated on _osdDismissed — see LayoutOsd.qml.
        MouseArea {
            anchors.fill: parent
            enabled: !root._osdDismissed
            onClicked: root.hide()
        }

    }

}
