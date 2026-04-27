// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * Navigation OSD content — Item-rooted body for use either inside the
 * standalone NavigationOsd Window or inside the unified NotificationOverlay
 * host that swaps OSD/picker modes via Loader.
 *
 * Owns the toast's data properties, animations, dismiss timer, show/hide
 * functions, and content tree. The hosting Window is a thin shell that
 * forwards C++ property writes via aliases and binds Qt.WindowTransparentForInput
 * to the public `dismissed` state.
 */
Item {
    id: root

    // Navigation feedback data
    property bool success: true
    property string action: "" // "move", "focus", "push", "restore", "float", "swap", "rotate", "snap", "cycle", "algorithm", "swap_vs", "rotate_vs"
    property string reason: "" // Failure reason if !success, direction for rotation (clockwise/counterclockwise), or float state (floated/unfloated)
    // Zone data
    property var zones: []
    property var highlightedZoneIds: [] // Zone IDs involved (target zones)
    property string sourceZoneId: "" // Source zone for move/swap operations
    property int windowCount: 1 // Number of windows affected (for rotation)
    // Timing
    property int displayDuration: 1000
    // ms before auto-hide (shorter than layout OSD). Show/hide fade
    // shapes are driven by the "osd.show" / "osd.hide" / "osd.pop"
    // profile JSONs — tune those rather than re-introducing per-window
    // duration overrides here.
    // Theme colors
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightColor: Kirigami.Theme.highlightColor
    property color errorColor: Kirigami.Theme.negativeTextColor
    // Per-instance fade overrides forwarded from the host Window.
    property int fadeInDuration: -1
    property int fadeOutDuration: -1
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
            if (action === "move" || action === "focus") {
                return i18n("No zone in that direction");
            } else if (action === "push") {
                return i18n("No empty zone available");
            } else if (action === "rotate") {
                return i18n("Nothing to rotate");
            } else if (action === "swap") {
                return i18n("Nothing to swap");
            } else if (action === "swap_vs") {
                if (reason === "no_subdivision" || reason === "not_virtual")
                    return i18n("No virtual screen split on this monitor");

                if (reason === "unknown_vs")
                    return i18n("Virtual screen no longer exists");

                return i18n("No adjacent virtual screen");
            } else if (action === "rotate_vs") {
                // Mirror swap_vs: both "not_virtual" (caller passed a
                // non-physical id) and "no_subdivision" (monitor has <2 VSs,
                // or physId unknown to Settings) surface the same user-
                // facing reason. Divergence here produced confusing copy
                // when rotating on an unsplit monitor vs swapping on one.
                if (reason === "not_virtual" || reason === "no_subdivision")
                    return i18n("No virtual screen split on this monitor");

                return i18n("No virtual screens to rotate");
            } else if (action === "focus_master")
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
        } else if (action === "swap_vs") {
            var vsSwapArrow = directionArrow(reason);
            return vsSwapArrow + " " + i18n("Virtual screens swapped");
        } else if (action === "rotate_vs") {
            var vsRotateArrow = (reason === "clockwise") ? "↻" : "↺";
            return vsRotateArrow + " " + i18n("Virtual screens rotated");
        } else {
            return i18n("Action completed");
        }
    }
    // Public dismiss state used by the hosting Window's flags binding to
    // toggle Qt.WindowTransparentForInput. True while the OSD is logically
    // hidden (pre-first-show or after a full hide animation), cleared by
    // show(), set again at the end of hideAnimation. NOT tied to opacity —
    // discrete flip on show/dismiss only, so the host's input-region flag
    // doesn't churn during the fade.
    property bool dismissed: true
    // Content-driven desired size, exposed for the hosting Window (or Loader
    // host) to read after C++ writes action/reason/zones. Mirrors the live
    // measurement of container's content so the host can size its layer
    // surface to fit the rendered text.
    readonly property int contentDesiredWidth: container.width + Math.round(Kirigami.Units.gridUnit * 2.5)
    readonly property int contentDesiredHeight: container.height + Math.round(Kirigami.Units.gridUnit * 2.5)

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
        // Reset state for fresh animation
        root.opacity = 0;
        container.scale = 0.8;
        root.dismissed = false;
        showAnimation.start();
        dismissTimer.restart();
    }

    // Hide the OSD with animation. After first show, root.opacity is the
    // sole driver of the visible/invisible toggle — the host Window stays
    // Qt-visible to preserve the Wayland layer-shell wl_surface and Vulkan
    // swap chain across hide/show cycles, with input-region releases
    // gated on `dismissed` via the host's flags binding.
    function hide() {
        if (root.dismissed)
            return ;

        showAnimation.stop();
        dismissTimer.stop();
        hideAnimation.start();
    }

    // Animated by show/hide animations. Item.opacity works on Wayland where
    // Window.opacity does not — the previous standalone NavigationOsd.qml
    // wrapped the content in a contentWrapper Item for exactly this reason;
    // now that the root IS that Item, the wrapper layer is gone.
    opacity: 0

    // Auto-dismiss timer
    Timer {
        id: dismissTimer

        interval: root.displayDuration
        onTriggered: root.hide()
    }

    // Show animation — see matching comment in LayoutOsd.qml for the
    // osd.show / osd.pop split rationale (preserves the OutBack scale
    // overshoot from the pre-PhosphorMotion design). durationOverride
    // binds to root.fadeInDuration / fadeOutDuration so consumers that
    // override those properties still drive the timing.
    ParallelAnimation {
        id: showAnimation

        PhosphorMotionAnimation {
            target: root
            properties: "opacity"
            from: 0
            to: 1
            profile: "osd.show"
            durationOverride: root.fadeInDuration
        }

        PhosphorMotionAnimation {
            target: container
            properties: "scale"
            from: 0.8
            to: 1
            profile: "osd.pop"
            durationOverride: root.fadeInDuration
        }

    }

    // Hide animation
    SequentialAnimation {
        id: hideAnimation

        ParallelAnimation {
            PhosphorMotionAnimation {
                target: root
                properties: "opacity"
                to: 0
                profile: "osd.hide"
                durationOverride: root.fadeOutDuration
            }

            PhosphorMotionAnimation {
                target: container
                properties: "scale"
                to: 0.9
                profile: "osd.hide"
                durationOverride: root.fadeOutDuration
            }

        }

        ScriptAction {
            script: {
                // Flip dismissed so the host Window's flags binding engages
                // Qt.WindowTransparentForInput. Window.visible is intentionally
                // not toggled — see the host's hide()-related comment.
                root.dismissed = true;
            }
        }

    }

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
        color: Qt.rgba(root.backgroundColor.r, root.backgroundColor.g, root.backgroundColor.b, 0.95)
        radius: Kirigami.Units.gridUnit * 1.5
        border.color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.15)
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
            color: root.success ? root.textColor : root.errorColor
            horizontalAlignment: Text.AlignHCenter
        }

    }

    // Click to dismiss. Gated on dismissed so that in the post-hide
    // transparent state we don't consume events at the Qt Quick level
    // even if the wl_surface input region hasn't been updated yet.
    MouseArea {
        anchors.fill: parent
        enabled: !root.dismissed
        onClicked: root.hide()
    }

}
