// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Navigation OSD content — Item-rooted body for use inside the
 * PassiveOverlayShell host that swaps OSD modes via its osdSlot Loader.
 *
 * Phase 5: surface lifecycle + show/hide animations are driven entirely by
 * PhosphorAnimationLayer::SurfaceAnimator (registered for the notification
 * scope with the shared OSD config — `osd.show` / `osd.pop` / `osd.hide`).
 * The library handles the visual fade by animating Window.contentItem
 * opacity + scale on the host surface, and PhosphorLayer::Surface handles
 * `Qt.WindowTransparentForInput` on the underlying QWindow during hide.
 *
 * This Item only owns:
 *   - Data properties written by C++ (action, reason, zones, …)
 *   - Computed messageText + container layout
 *   - The auto-dismiss Timer + dismissRequested signal, forwarded by the
 *     shell host as `osdDismissRequested` and routed by C++ to
 *     OverlayService::onOsdDismissRequested → ShellHost::hideSlot
 */
Item {
    id: root

    // ── Data properties ───────────────────────────────────────────────────
    property bool success: true
    property string action: "" // one of the tokens handled by messageText(): "rotate", "move", "focus", "swap", "push", "restore", "float", "snap", "cycle", "focus_master", "swap_master", "master_ratio", "master_count", "retile", "resnap", "algorithm", "swap_vs", "rotate_vs"
    property string reason: "" // Failure reason if !success, direction for rotation (clockwise/counterclockwise), or float state (floated/unfloated)
    property var zones: []
    property var highlightedZoneIds: [] // Zone IDs involved (target zones)
    property string sourceZoneId: "" // Source zone for move/swap operations
    property int windowCount: 1 // Number of windows affected (for rotation)
    // Auto-dismiss interval. Show/hide fade shapes are owned by the
    // SurfaceAnimator's `osd.show` / `osd.pop` / `osd.hide` profile JSONs;
    // tune the JSONs to adjust the appear/disappear feel rather than
    // re-introducing per-window duration overrides here.
    property int displayDuration: 1000
    // Theme colors
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    // Unread by this content body, but part of the OSD slot forwarding
    // contract: PassiveOverlayShell's navigationOsdComp binds it from
    // osdSlot.highlightColor, which C++ writes (osd.cpp
    // pushLayoutOsdContent) on the shared OSD slot. Deleting it would
    // break the shell binding, so it stays declared.
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
    /// Auto-dismiss request emitted by the dismissTimer / click MouseArea.
    /// The unified shell host re-emits this as its `osdDismissRequested`
    /// signal, which C++ (wirePassiveShellSlots) routes to
    /// OverlayService::onOsdDismissRequested → ShellHost::hideSlot for an
    /// animator-driven slot-hide.
    signal dismissRequested

    /// Restart the auto-dismiss timer from C++ on every show. Forwards to
    /// the shared OsdDismissable helper so the latch reset is driven off
    /// the timer's runningChanged transition automatically.
    function restartDismissTimer() {
        dismiss.restart();
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
        // Cross-surface moves prefix the direction with the surface they cross:
        // "screen:left", "desktop:right". Strip it to the bare token so the
        // arrow matches the actual direction (an unstripped "screen:left" used
        // to fall through to the default "→", pointing the wrong way).
        const token = dir.indexOf(":") >= 0 ? dir.slice(dir.indexOf(":") + 1) : dir;
        switch (token) {
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

    Accessible.name: i18n("Navigation feedback")
    Accessible.description: i18n("Brief feedback when using keyboard navigation to move or focus windows between zones")

    // Auto-dismiss timer + idempotency latch. See OsdDismissable.qml for
    // why the latch is needed (timer-fire and click both race to dismiss).
    OsdDismissable {
        id: dismiss

        interval: root.displayDuration
        onRequest: root.dismissRequested()
    }

    // The OSD card. QFZCommon.PopupFrame owns the background, border,
    // soft glow, and the SurfaceAnimator shader anchor — the same chrome
    // the layout-picker and zone-selector popups use. PopupFrame's
    // internal captureItem extends past the frame so the glow is part of
    // the captured texture and travels with the card through bounce /
    // fly-in / etc. instead of snapping in when the leg ends. Matches
    // LayoutOsdContent; the a11y labels live on the root Item.
    QFZCommon.PopupFrame {
        id: container

        anchors.centerIn: parent
        // Text-only: size based on message content
        width: Math.max(messageLabel.implicitWidth + Kirigami.Units.gridUnit * 3, Kirigami.Units.gridUnit * 10)
        height: messageLabel.implicitHeight + Kirigami.Units.gridUnit * 2.5
        backgroundColor: root.backgroundColor
        containerRadius: Kirigami.Units.gridUnit * 1.5

        // Message label - informative text-based feedback
        Label {
            id: messageLabel

            Accessible.name: root.messageText
            anchors.top: parent.top
            anchors.topMargin: Kirigami.Units.gridUnit * 1.5
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.messageText
            font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.3
            font.weight: Font.Medium
            color: root.success ? root.textColor : root.errorColor
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // Click the card to dismiss. Anchored to the card (not the whole OSD
    // slot) so a concurrent modal slot (snap assist, picker) keeps receiving
    // its own clicks instead of hitting a screen-wide input shield.
    // dismiss.fire() collapses timer-fire + click into a single
    // dismissRequested per show cycle via the shared latch.
    MouseArea {
        anchors.fill: container
        onClicked: dismiss.fire()
        Accessible.role: Accessible.Button
        Accessible.name: i18n("Dismiss notification")
    }
}
