// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.phosphor.animation

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
    // Phase 5: surface lifecycle + show/hide animations are entirely library-
    // driven. PhosphorAnimationLayer::SurfaceAnimator (registered for
    // PzRoles::NavigationOsd, same shape as LayoutOsd) drives Window.contentItem
    // opacity + scale via its `osd.show` / `osd.pop` / `osd.hide` profiles;
    // PhosphorLayer::Surface handles `Qt.WindowTransparentForInput` on the
    // underlying QWindow during the hide cycle.

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
    // Content-driven desired size, exposed for C++ to read after writing
    // action/reason/zones. Mirrors the width/height bindings below but stays
    // live even when C++ later calls setWidth/setHeight (which detaches the
    // Window width binding but leaves this readonly property intact). Used
    // by OverlayService::showNavigationOsd to size the layer window based
    // on the rendered message length rather than a hardcoded constant.
    readonly property int contentDesiredWidth: container.width + Math.round(Kirigami.Units.gridUnit * 2.5)
    readonly property int contentDesiredHeight: container.height + Math.round(Kirigami.Units.gridUnit * 2.5)

    /// Auto-dismiss request emitted by the dismissTimer. C++ side hooks this
    /// to OverlayService → Surface::hide().
    signal dismissRequested()

    /// Restart the auto-dismiss timer from C++ on every show.
    function restartDismissTimer() {
        dismissTimer.restart();
    }

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

    // Window configuration — Phase 5 surface lifecycle owns
    // Qt.WindowTransparentForInput on the underlying QWindow during hide.
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    // Size based on container (which is inside contentWrapper)
    width: container.width + Math.round(Kirigami.Units.gridUnit * 2.5)
    height: container.height + Math.round(Kirigami.Units.gridUnit * 2.5)
    // Start hidden; first Surface::show() flips visible=true.
    visible: false

    // Auto-dismiss timer. Emits a signal that C++ hooks to Surface::hide().
    Timer {
        id: dismissTimer

        interval: root.displayDuration
        onTriggered: root.dismissRequested()
    }

    // (Phase 5: showAnimation / hideAnimation removed — library drives.)
    Item {
        id: _phase5Placeholder
    }

    // Content wrapper. Opacity defaults to 1 — the SurfaceAnimator drives
    // window.contentItem opacity for show/hide.
    Item {
        id: contentWrapper

        anchors.fill: parent

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

        // Click to dismiss. With Phase-5 surface lifecycle the post-hide
        // input gate is enforced at the QWindow level via
        // Qt.WindowTransparentForInput.
        MouseArea {
            anchors.fill: parent
            onClicked: root.dismissRequested()
        }

    }

}
