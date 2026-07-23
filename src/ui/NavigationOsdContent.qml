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
    property string action: "" // one of the tokens handled by messageText(): "rotate", "move", "span", "focus", "swap", "push", "restore", "float", "snap", "cycle", "focus_master", "swap_master", "master_ratio", "master_count", "retile", "resnap", "snap_assist", "snap_all", "swap_vs", "rotate_vs"
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
            // Failure messages. Shared strings first: producers reuse these
            // reason tokens across actions, so the copy is defined once.
            const noWindowText = i18n("No window is focused");
            const noLayoutText = i18n("No zone layout on this screen");
            const unavailableText = i18n("Zone navigation is unavailable");
            const isInternalReason = reason === "engine_unavailable" || reason === "invalid_direction" || reason === "geometry_error" || reason === "no_zone_detection";
            if (reason === "excluded") {
                // Shared by move/swap/push/snap/span: the focused window
                // is excluded by a rule or below the minimum size.
                return i18n("This window is excluded from tiling");
            }
            if (action === "move" || action === "focus" || action === "span") {
                // Layout-level failures are not a direction problem: telling
                // the user "no zone in that direction" would suggest another
                // arrow key could work when no layout is active at all.
                if (reason === "no_zones" || reason === "no_active_layout")
                    return noLayoutText;

                // Autotile emits no_windows / no_focus / nothing_to_swap when
                // there is no tiled window to act on; direction copy would
                // wrongly suggest another arrow key could work.
                if (reason === "no_window" || reason === "no_windows" || reason === "no_focus" || reason === "nothing_to_swap")
                    return noWindowText;

                if (reason === "not_snapped")
                    return i18n("Window is not in a zone");

                if (reason === "no_window_in_zone")
                    return i18n("No window in that direction");

                if (action === "span" && reason === "not_supported")
                    return i18n("Spanning is not available in autotile mode");

                if (isInternalReason)
                    return unavailableText;

                return i18n("No zone in that direction");
            } else if (action === "push") {
                if (reason === "no_window")
                    return noWindowText;

                if (isInternalReason)
                    return unavailableText;

                return i18n("No empty zone available");
            } else if (action === "snap") {
                if (reason === "no_window" || reason === "no_focus" || reason === "no_windows")
                    return noWindowText;

                if (reason === "no_active_layout")
                    return noLayoutText;

                if (reason === "invalid_zone_number" || reason === "zone_not_found")
                    return i18n("No zone with that number");

                if (reason === "already_at_position")
                    return i18n("Window is already in that position");

                return unavailableText;
            } else if (action === "float") {
                if (reason === "no_active_window" || reason === "no_focused_window" || reason === "no_window" || reason === "window_not_tracked" || reason === "invalid_window")
                    return noWindowText;

                if (reason === "no_pre_float_zone")
                    return i18n("No zone to return to");

                return i18n("Floating is unavailable");
            } else if (action === "cycle") {
                if (reason === "single_window")
                    return i18n("No other window in this zone");

                if (reason === "no_neighbor")
                    return i18n("No other window");

                if (reason === "not_snapped")
                    return i18n("Window is not in a zone");

                if (isInternalReason)
                    return unavailableText;

                return noWindowText;
            } else if (action === "restore") {
                if (reason === "no_window")
                    return noWindowText;

                if (isInternalReason)
                    return unavailableText;

                return i18n("Nothing to restore");
            } else if (action === "resnap") {
                if (reason === "no_active_layout")
                    return noLayoutText;

                return i18n("No windows to rearrange");
            } else if (action === "master_ratio" || action === "master_count") {
                // The producer deliberately reports success=false at the
                // clamp bound while still carrying the clamped value in the
                // reason ("increased:NN"); show the value at its limit
                // instead of a generic failure.
                const boundParts = reason.split(":");
                if (boundParts.length >= 2)
                    return action === "master_ratio" ? i18n("Master ratio at limit (%1%)", boundParts[1]) : i18n("Master count at limit (%1)", boundParts[1]);

                return noWindowText;
            } else if (action === "rotate") {
                if (reason === "no_active_layout")
                    return noLayoutText;

                if (isInternalReason)
                    return unavailableText;

                return i18n("Nothing to rotate");
            } else if (action === "swap") {
                if (reason === "no_window" || reason === "no_focus" || reason === "no_windows")
                    return noWindowText;

                if (reason === "not_snapped")
                    return i18n("Window is not in a zone");

                if (isInternalReason)
                    return unavailableText;

                return i18n("Nothing to swap");
            } else if (action === "snap_assist") {
                return noWindowText;
            } else if (action === "snap_all") {
                if (reason === "no_unsnapped_windows")
                    return i18n("All windows are already in zones");

                return unavailableText;
            } else if (action === "swap_vs") {
                if (reason === "no_subdivision" || reason === "not_virtual")
                    return i18n("No virtual screen split on this monitor");

                if (reason === "unknown_vs")
                    return i18n("Virtual screen no longer exists");

                // Internal write/validation errors are not a topology fact;
                // only no_sibling means there is genuinely no neighbour.
                if (reason === "swap_failed" || reason === "settings_rejected" || reason === "no_config_store" || reason === "invalid_direction")
                    return i18n("Virtual screen swap failed");

                return i18n("No adjacent virtual screen");
            } else if (action === "rotate_vs") {
                // Mirror swap_vs: both "not_virtual" (caller passed a
                // non-physical id) and "no_subdivision" (monitor has <2 VSs,
                // or physId unknown to Settings) surface the same user-
                // facing reason. Divergence here produced confusing copy
                // when rotating on an unsplit monitor vs swapping on one.
                if (reason === "not_virtual" || reason === "no_subdivision")
                    return i18n("No virtual screen split on this monitor");

                // Same internal-error split as swap_vs above.
                if (reason === "swap_failed" || reason === "settings_rejected" || reason === "no_config_store" || reason === "invalid_direction")
                    return i18n("Virtual screen rotation failed");

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
        } else if (action === "span") {
            // reason format: "grow:right", "shrink:left", or "snap:right"
            // (the last one when span snapped a previously unsnapped window)
            var spanArrow = directionArrow(reason);
            if (reason.indexOf("grow") === 0) {
                if (targetZoneNumber > 0)
                    return spanArrow + " " + i18n("Extended into Zone %1", targetZoneNumber);

                return spanArrow + " " + i18n("Span extended");
            }
            if (reason.indexOf("snap") === 0) {
                if (targetZoneNumber > 0)
                    return spanArrow + " " + i18n("Snapped: Zone %1", targetZoneNumber);

                return spanArrow + " " + i18n("Snapped");
            }
            return spanArrow + " " + i18n("Span reduced");
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

    // The OSD card. QFZCommon.PopupFrame owns the opaque card body and the
    // SurfaceAnimator shader anchor; border, glow, and shadow come from the
    // surface-decoration pipeline. PopupFrame's internal captureItem extends
    // past the frame by captureMargin so any decoration halo and the show /
    // hide shader transition are captured with the card through bounce /
    // fly-in / etc. instead of snapping in when the leg ends. Matches
    // LayoutOsdContent; the a11y labels live on the root Item.
    QFZCommon.PopupFrame {
        id: container

        anchors.centerIn: parent
        // Text-only: size based on message content
        width: Math.max(messageLabel.implicitWidth + Kirigami.Units.gridUnit * 3, Kirigami.Units.gridUnit * 10)
        height: messageLabel.implicitHeight + Kirigami.Units.gridUnit * 2.5
        backgroundColor: root.backgroundColor

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
