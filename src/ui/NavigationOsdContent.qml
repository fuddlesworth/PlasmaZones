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
    // One of the tokens handled by successMessage() and/or failureMessage():
    // "rotate", "move", "span", "focus", "swap", "push", "restore", "float",
    // "snap", "cycle", "focus_master", "swap_master", "master_ratio",
    // "master_count", "retile", "resnap", "resize", "tabbed", "fullscreen",
    // "consume", "expel", "center", "snap_assist", "snap_all", "swap_vs",
    // "rotate_vs", "layout" ("layout" is failure-only by producer contract;
    // see failureMessage).
    property string action: ""
    property string reason: "" // Failure reason if !success, direction for rotation (clockwise/counterclockwise) or travel ("left"/"right"/"up"/"down", optionally prefixed "screen:" or "desktop:" for a crossing), float state (floated/floating/tiled/snapped/unfloated/overflow), or windowed-fullscreen state ("on"/"off")
    property var zones: []
    property var highlightedZoneIds: [] // Zone IDs involved (target zones; a WINDOW id for the float-family actions)
    property string sourceZoneId: "" // Source zone for move/swap operations (a WINDOW id for the float-family actions)
    property int windowCount: 1 // Number of windows affected (for rotation)
    // Auto-dismiss interval — a local constant (readonly: no C++ path
    // writes it and the shell does not forward it). Show/hide fade shapes
    // are owned by the SurfaceAnimator's `osd.show` / `osd.pop` /
    // `osd.hide` profile JSONs; tune the JSONs to adjust the
    // appear/disappear feel rather than re-introducing per-window
    // duration overrides here.
    readonly property int displayDuration: 1000
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
    // User overlay font settings, forwarded by PassiveOverlayShell's
    // navigationOsdComp and written by the C++ nav-OSD show path — the same
    // contract every other content body on this slot follows.
    property string fontFamily: ""
    property real fontSizeScale: 1
    /// Message type scale. Kirigami has no OSD-headline constant, so the
    /// factor is named here rather than buried in the Label binding.
    readonly property real messageFontScale: 1.3
    // The master ratio/count producers report success=false at the clamp
    // bound while carrying the clamped value ("increased:NN") — that copy is
    // informational ("here is your value at its limit"), not an error, so
    // the label must not paint it in the error colour beside genuine
    // failures like "No window is focused".
    readonly property bool atClampBound: !success && (action === "master_ratio" || action === "master_count") && reason.split(":").length >= 2
    // Get target zone number (first highlighted zone)
    readonly property int targetZoneNumber: {
        if (highlightedZoneIds && highlightedZoneIds.length > 0)
            return getZoneNumber(highlightedZoneIds[0]);

        return -1;
    }
    // Get source zone number
    readonly property int sourceZoneNumber: getZoneNumber(sourceZoneId)
    // Computed message text. The action/reason vocabulary is large enough
    // that the failure and success halves are separate functions; this
    // property only picks between them.
    readonly property string messageText: success ? successMessage() : failureMessage()
    /// Failure copy for the current action/reason pair. Producers reuse
    /// reason tokens across actions, so the shared strings are defined once
    /// at the top and each action branch overrides only what differs.
    function failureMessage(): string {
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

            // Autotile emits not_tiled for a focused-but-floating
            // window; only "move" reaches here (swap has its own branch).
            if (reason === "not_tiled")
                return i18n("Window is floating");

            if (reason === "no_window_in_zone" || reason === "no_neighbor")
                return i18n("No window in that direction");

            // The scroll engine's universal "the strip has nothing there"
            // refusal: pressing move/focus at the strip's edge is the
            // ordinary single-monitor case, and the zone fallthrough below
            // would name zones a scrolling screen does not have.
            if (reason === "no_target")
                return i18n("No window in that direction");

            if (reason === "swap_failed")
                return i18n("Could not move the window");

            if (action === "span" && reason === "not_supported")
                return i18n("Spanning is not available in this mode");

            if (isInternalReason)
                return unavailableText;

            return i18n("No zone in that direction");
        } else if (action === "push") {
            if (reason === "no_window")
                return noWindowText;

            // Autotile and scrolling report the intent as unsupported; the
            // zone fallthrough below would wrongly promise an empty zone
            // exists in modes that have no zones.
            if (reason === "not_supported")
                return i18n("Pushing to an empty zone is not available in this mode");

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

            // Autotile: the focused window is a float, so a tiled-position
            // digit has nothing to reorder (same phrasing as the swap arm).
            if (reason === "not_tiled")
                return i18n("Window is floating");

            // Scrolling: the digit named a strip position that does not
            // exist right now.
            if (reason === "no_target")
                return i18n("No window in that position");

            return unavailableText;
        } else if (action === "float") {
            // no_windows rides this arm since the floating/tiled focus
            // switch moved onto the float token: an empty screen must say
            // so, not claim floating is unavailable.
            if (reason === "no_active_window" || reason === "no_focused_window" || reason === "no_window" || reason === "no_windows" || reason === "window_not_tracked" || reason === "invalid_window")
                return noWindowText;

            if (reason === "no_pre_float_zone")
                return i18n("No zone to return to");

            // The floating/tiling focus switch (all three engines) and
            // scrolling's explicit float verb: the layer asked for has no
            // window to take focus (the focused window is already there,
            // or the other layer is empty).
            // Exact copy for the two switch legs; approximate for
            // moveFocusedToFloating's already-floating refusal, where it is
            // still closer than "Floating is unavailable" was (a per-site
            // token would be the exact fix, at the cost of a producer
            // vocabulary split).
            if (reason === "no_target")
                return i18n("No window to switch to");

            // Also absorbs three named producer tokens the copy reads
            // correctly for: not_managed (snap + scroll engines' untracked
            // window), no_focused_screen and no_screen (autotile facade).
            return i18n("Floating is unavailable");
        } else if (action === "cycle") {
            if (reason === "single_window")
                return i18n("No other window in this zone");

            if (reason === "no_neighbor" || reason === "no_target")
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

            if (reason === "not_tiled")
                return i18n("Window is floating");

            if (reason === "swap_failed")
                return i18n("Could not swap windows");

            if (reason === "no_neighbor")
                return i18n("No window in that direction");

            if (reason === "no_adjacent_zone")
                return i18n("No zone in that direction");

            if (isInternalReason)
                return unavailableText;

            return i18n("Nothing to swap");
        } else if (action === "snap_assist") {
            // Sole live reason is window_not_found: the picked window
            // vanished before placement.
            return i18n("That window is no longer available");
        } else if (action === "snap_all") {
            // Mode-neutral on purpose: BOTH producers reach this arm — the
            // scrolling strip emits navigationFeedback directly, and snap's
            // snap_all arrives via the KWin effect's reportNavigationFeedback
            // relay (snaphandler.cpp) with the same tokens — and the OSD is
            // not told the mode. Same resolution as the shortcut's neutral
            // "Arrange All Windows" label in the catalog.
            if (reason === "no_unsnapped_windows")
                return i18n("All windows are already arranged");

            return i18n("Could not arrange the windows");
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
        } else if (action === "layout") {
            // Daemon-level gate: a layout-selection shortcut (picker, cycle,
            // quick slot, layout lock) fired on a screen whose engine has no
            // layout concept (IPlacementEngine::layoutSupport is None), or
            // one whose engine browses templates and has none to offer.
            // no_templates means the template store is empty, which the user
            // can act on; not_supported is the capability-less engine. The
            // fallthrough keeps any future reason from rendering the generic
            // "Failed".
            if (reason === "no_templates")
                return i18n("No column templates available");

            // A quick slot whose bound layout the picker no longer offers on
            // this screen (hidden from the selector, excluded by an
            // allow-list or the aspect filter, or an algorithm that is no
            // longer installed). Distinct from the capability cases above:
            // the mode is fine, the binding is not.
            if (reason === "slot_unavailable")
                return i18n("That layout is not available on this screen");

            return i18n("Layouts are not available in this mode");
        } else if (action === "resize") {
            // Scrolling width AND height verbs share this token, so the
            // copy names neither a column nor a window. no_target for the
            // preset cycle means the vocabulary offers no other value (a
            // single-entry template list is refused by design, not broken).
            if (reason === "no_target")
                return i18n("Already at that size");

            if (reason === "no_window" || reason === "no_windows" || reason === "no_focus")
                return noWindowText;

            return i18n("Resizing is unavailable");
        } else if (action === "tabbed") {
            // no_target: the producer's changed=false leg (toggleActiveColumnTabbed
            // refuses only when no column is focused), same as the fullscreen twin.
            if (reason === "no_window" || reason === "no_windows" || reason === "no_focus" || reason === "no_target")
                return noWindowText;

            return i18n("Tabbing is unavailable");
        } else if (action === "fullscreen") {
            if (reason === "no_window" || reason === "no_windows" || reason === "no_focus" || reason === "no_target")
                return noWindowText;

            // Unreachable today (the producer emits only the reasons above);
            // kept as the arm's fallthrough against a future producer reason,
            // mirroring the "Tabbing is unavailable" arm's shape.
            return i18n("Windowed fullscreen is unavailable");
        } else if (action === "consume" || action === "expel") {
            if (reason === "no_window" || reason === "no_windows" || reason === "no_focus")
                return noWindowText;

            return i18n("No window to move between columns");
        } else if (action === "center") {
            if (reason === "no_window" || reason === "no_windows" || reason === "no_focus")
                return noWindowText;

            // Both centering verbs (single column and the visible group)
            // collapse onto one no_target token at the producer, so the
            // copy stays count-neutral.
            return i18n("Already centered");
        } else if (action === "retile") {
            return i18n("Could not refresh the layout");
        } else if (action === "focus_master") {
            // Unreachable today (the sole producer emits only no_windows);
            // kept as the arm's routing for a future internal token,
            // mirroring the sibling arms' isInternalReason handling.
            if (isInternalReason)
                return unavailableText;

            return i18n("No windows to focus");
        } else if (action === "swap_master") {
            if (reason === "already_master")
                return i18n("Already in master position");

            if (reason === "no_focus")
                return noWindowText;

            // The focused window is a float; promoting it to master would
            // reorder nothing on screen (same phrasing as the swap arm).
            if (reason === "not_tiled")
                return i18n("Window is floating");

            return i18n("Nothing to swap");
        } else
            return i18n("Failed");
    }

    /// Success copy for the current action, including the zone numbers and
    /// direction arrows the reason token carries. No "layout" arm on
    /// purpose: that action is failure-only by producer contract. All four
    /// emitters (Daemon::showLayoutsUnavailableOsd, the two no_templates
    /// sites in shortcuts_wiring.cpp and start.cpp, and the slot_unavailable
    /// site in shortcuts_wiring.cpp) hardcode success=false.
    /// snap_all, snap_assist and resnap are equally failure-only (every
    /// emitter in snaphandler.cpp, engine_navigation.cpp and the snap
    /// engine's navigation.cpp sends success=false) and equally arm-less —
    /// a future success emitter for any of the four needs an arm here or it
    /// renders as the generic "Action completed".
    function successMessage(): string {
        if (action === "rotate") {
            const arrow = rotationArrow(reason);
            if (windowCount > 1)
                return glyphed(arrow, i18np("Rotated %n window", "Rotated %n windows", windowCount));
            else
                return glyphed(arrow, i18n("Rotated"));
        } else if (action === "move") {
            const moveArrow = directionArrow(reason);
            if (targetZoneNumber > 0)
                return glyphed(moveArrow, i18n("Zone %1", targetZoneNumber));

            return glyphed(moveArrow, i18n("Moved"));
        } else if (action === "span") {
            // reason format: "grow:right", "shrink:left", or "snap:right"
            // (the last one when span snapped a previously unsnapped window)
            const spanArrow = directionArrow(reason);
            if (reason.indexOf("grow") === 0) {
                if (targetZoneNumber > 0)
                    return glyphed(spanArrow, i18n("Extended into Zone %1", targetZoneNumber));

                return glyphed(spanArrow, i18n("Span extended"));
            }
            if (reason.indexOf("snap") === 0) {
                if (targetZoneNumber > 0)
                    return glyphed(spanArrow, i18n("Snapped into Zone %1", targetZoneNumber));

                return glyphed(spanArrow, i18nc("@info:status the window was snapped into a zone", "Snapped"));
            }
            return glyphed(spanArrow, i18n("Span reduced"));
        } else if (action === "focus") {
            // A directional press whose focus was not in the tiled set enters
            // at the master rather than travelling, and says so.
            if (reason === "master")
                return i18n("Focus on the master window");

            const focusArrow = directionArrow(reason);
            if (targetZoneNumber > 0)
                return glyphed(focusArrow, i18n("Focus on Zone %1", targetZoneNumber));

            return glyphed(focusArrow, i18nc("@info:status focus moved in the pressed direction", "Focus"));
        } else if (action === "swap") {
            const swapArrow = directionArrow(reason);
            if (sourceZoneNumber > 0 && targetZoneNumber > 0)
                return glyphed(swapArrow, i18n("Zone %1 ↔ Zone %2", sourceZoneNumber, targetZoneNumber));

            return glyphed(swapArrow, i18n("Swapped"));
        } else if (action === "push") {
            if (targetZoneNumber > 0)
                return glyphed("→", i18n("Zone %1", targetZoneNumber));

            return i18n("Window pushed");
        } else if (action === "restore") {
            return i18nc("@info:status the window's previous position was restored", "Restored");
        } else if (action === "float") {
            // Show different message based on float state from reason field.
            // The "tiled" arm and the "Floating" fallthrough at the bottom
            // serve BOTH the float toggle and the layer-focus switch (the
            // fallthrough also absorbs the toggle's "floated" token): the
            // producers do not distinguish switch-origin from toggle-origin
            // on these tokens, and splitting the copy would need a producer
            // token split (the same tradeoff the no_target comment above
            // weighs and declines).
            if (reason === "tiled")
                return i18nc("@info:status the window is now tiled (adjective, not a verb)", "Tiled");

            if (reason === "unfloated")
                return i18nc("@info:status the window was snapped into a zone", "Snapped");

            // The snap engine's floating/snapped focus switch: focus moved
            // to the zone layer, which snap users know as "snapped", not
            // "tiled".
            if (reason === "snapped")
                return i18nc("@info:status the snapped layer took focus", "Snapped");

            // Autotile auto-floats windows that overflow the layout; the
            // generic copy would read as a deliberate float of the focused
            // window.
            if (reason === "overflow")
                return i18n("Extra windows moved out of the layout");

            return i18nc("@info:status the window is now floating (adjective, not a verb)", "Floating");
        } else if (action === "snap") {
            if (targetZoneNumber > 0)
                return i18n("Snapped into Zone %1", targetZoneNumber);

            return i18nc("@info:status the window was snapped into a zone", "Snapped");
        } else if (action === "cycle") {
            return i18n("Next window");
        } else if (action === "focus_master") {
            return i18n("Focused the master window");
        } else if (action === "swap_master") {
            return i18n("Swapped with master window");
        } else if (action === "master_ratio") {
            // reason format: "increased:65" or "decreased:60"
            const parts = reason.split(":");
            const pct = parts.length >= 2 ? parts[1] : "";
            return pct ? i18n("Master ratio → %1%", pct) : i18n("Master ratio changed");
        } else if (action === "master_count") {
            const parts = reason.split(":");
            const count = parts.length >= 2 ? parts[1] : "";
            return count ? i18n("Master count → %1", count) : i18n("Master count changed");
        } else if (action === "retile") {
            return i18n("Layout refreshed");
        } else if (action === "resize") {
            return i18nc("@info:status the window was resized", "Resized");
        } else if (action === "tabbed") {
            return i18n("Tabbed display toggled");
        } else if (action === "fullscreen") {
            // The producer reads the resulting state back off the strip and
            // sends it as the reason, so the OSD can say which way the
            // toggle went (an empty reason would only support a generic
            // "toggled").
            if (reason === "off")
                return i18n("Windowed fullscreen off");

            return i18n("Windowed fullscreen on");
        } else if (action === "consume") {
            return i18n("Window moved between columns");
        } else if (action === "expel") {
            return i18n("Window expelled into its own column");
        } else if (action === "center") {
            // "span" is centerVisibleColumns' whole-group variant; the bare
            // reason is centerColumn.
            if (reason === "span")
                return i18n("Visible columns centered");

            return i18n("Column centered");
        } else if (action === "swap_vs") {
            const vsSwapArrow = directionArrow(reason);
            return glyphed(vsSwapArrow, i18n("Virtual screens swapped"));
        } else if (action === "rotate_vs") {
            const vsRotateArrow = rotationArrow(reason);
            return glyphed(vsRotateArrow, i18n("Virtual screens rotated"));
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
    function normalizeUuid(uuid: string): string {
        if (!uuid)
            return "";

        const s = String(uuid);
        // Remove leading/trailing braces if present
        if (s.startsWith("{") && s.endsWith("}"))
            return s.substring(1, s.length - 1).toLowerCase();

        return s.toLowerCase();
    }

    // Helper function to get zone number from zone ID
    function getZoneNumber(zoneId: string): int {
        if (!zoneId || !zones || zones.length === 0)
            return -1;

        const normalizedTarget = normalizeUuid(zoneId);
        for (let i = 0; i < zones.length; i++) {
            const zone = zones[i];
            const id = zone.zoneId || zone.id || "";
            // Compare normalized UUIDs to handle format differences.
            // -1 (drop the number) when the record carries no zoneNumber:
            // both live producers stamp a positive one, and inventing a
            // display number from the array index would silently announce a
            // WRONG number if a producer ever stopped stamping the field
            // (zone identity is ids, never indices).
            if (normalizeUuid(id) === normalizedTarget)
                return zone.zoneNumber > 0 ? zone.zoneNumber : -1;
        }
        return -1;
    }

    /// Prefix @p text with @p glyph as one translatable unit, so translators
    /// control the order and RTL locales mirror it. One call site for the
    /// context string, so it cannot fork into near-duplicate msgids.
    function glyphed(glyph: string, text: string): string {
        return i18nc("@info:status glyph, then the message it labels", "%1 %2", glyph, text);
    }

    /// Rotation direction glyph. Separate from directionArrow, which maps the
    /// four travel directions.
    function rotationArrow(dir: string): string {
        return dir === "clockwise" ? "↻" : "↺";
    }

    /// Map a direction string ("left","right","up","down") to an arrow character.
    function directionArrow(dir: string): string {
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

    // The DYNAMIC message is the announcement (the ignored label below
    // carries no a11y payload of its own); the static explanation rides in
    // the description. Same composed-parent shape as CheatsheetContent's
    // shortcutRow: the parent that justifies the child's ignore is the
    // parent that carries the content, so it needs the role too.
    Accessible.role: Accessible.StaticText
    Accessible.name: root.messageText
    Accessible.description: i18n("Brief feedback for keyboard window and layout actions")

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
        // Text-only: size based on message content, clamped to the output so
        // long copy at a large font scale (fontSizeScale reaches 3.0) wraps
        // inside the screen instead of clipping at both centred ends. The
        // label wraps against the same bound, and the height follows its
        // wrapped implicitHeight, so the card grows downward as lines wrap.
        width: Math.min(Math.max(messageLabel.implicitWidth + Kirigami.Units.gridUnit * 3, Kirigami.Units.gridUnit * 10), root.width > 0 ? root.width - Kirigami.Units.gridUnit * 2 : Number.MAX_VALUE)
        // contentHeight, not implicitHeight: it is the height of the text as
        // laid out at its CURRENT width, so the card grows with wrapped lines
        // regardless of how Text's implicit-size machinery treats a wrap.
        // Clamped to the output like the width above — many wrapped lines at
        // fontSizeScale 3.0 on a portrait output would otherwise extend the
        // centred card past both screen edges (the label's maximumLineCount
        // degrades the clamped case to an ellipsis instead of a cut line).
        height: Math.min(messageLabel.contentHeight + Kirigami.Units.gridUnit * 3, root.height > 0 ? root.height - Kirigami.Units.gridUnit * 2 : Number.MAX_VALUE)
        backgroundColor: root.backgroundColor

        // Message label - informative text-based feedback
        Label {
            id: messageLabel

            // The root Item announces messageText (with the StaticText
            // role); without ignoring this label a screen reader walks the
            // same text twice (the sibling cheatsheet marks composed
            // children the same way).
            Accessible.ignored: true
            anchors.top: parent.top
            anchors.topMargin: Kirigami.Units.gridUnit * 1.5
            anchors.horizontalCenter: parent.horizontalCenter
            // Wrap against the clamped card width, unconditionally: binding
            // exactly implicitWidth in the unclamped case invites a
            // sub-pixel spurious wrap of the last word, while this leaves a
            // gridUnit of slack (the card is implicitWidth + 3 gridUnits
            // when unclamped) and AlignHCenter centres short copy anyway.
            width: container.width - Kirigami.Units.gridUnit * 2
            wrapMode: Text.WordWrap
            // Belt for the card's height clamp: cap the line count to what
            // fits the clamped card (output height minus the card's outer
            // margin and vertical padding), so the overflow degrades to an
            // ellipsis on the last VISIBLE line rather than a horizontally
            // cut line. Derived from the label's own line height so the cap
            // tracks fontSizeScale; unclamped outputs yield a cap far above
            // any current copy.
            maximumLineCount: root.height > 0 ? Math.max(1, Math.floor((root.height - Kirigami.Units.gridUnit * 5) / Math.max(1, messageMetrics.lineSpacing))) : 8
            elide: Text.ElideRight

            FontMetrics {
                id: messageMetrics
                font: messageLabel.font
            }
            text: root.messageText
            font.family: root.fontFamily.length > 0 ? root.fontFamily : Kirigami.Theme.defaultFont.family
            font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * root.messageFontScale * root.fontSizeScale)
            // The nav card deliberately fixes its headline weight and takes
            // only family/scale from the user's OSD font settings. C++ does
            // write all six font fields onto the shared osdSlot; what stops
            // the style flags (weight/italic/underline/strikeout) here is
            // PassiveOverlayShell's navigationOsdComp, which forwards only
            // family and scale. LayoutOsdContent forwards the flags to
            // ZonePreview's zone-number labels while its own name label
            // fixes Font.Medium the same way this one does.
            font.weight: Font.Medium
            color: (root.success || root.atClampBound) ? root.textColor : root.errorColor
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // Click the card to dismiss — BEST-EFFORT only: the OSD's host surface
    // is input-transparent whenever no modal slot is up (see the
    // anyInputGrabbing rationale in shellhost_bridge.cpp — a daemon that ate
    // every click for the OSD's lifetime was judged worse), so in the common
    // case this area receives nothing and the timer is the real dismiss.
    // Clicks land here only while a modal slot (snap assist, picker) has the
    // surface accepting input; the card anchoring keeps the modal's own
    // clicks out of a screen-wide shield in that case. dismiss.fire()
    // collapses timer-fire + click into a single dismissRequested per show
    // cycle via the shared latch. Keep the MouseArea: the daemon rationale
    // depends on it existing for the modal-visible case.
    MouseArea {
        anchors.fill: container
        onClicked: dismiss.fire()
        Accessible.role: Accessible.Button
        Accessible.name: i18n("Dismiss notification")
        // Without a press action an assistive client can see the button but
        // not activate it — same call the click handler makes, same wiring
        // as every sibling dismiss surface (picker, cheatsheet, snap assist).
        Accessible.onPressAction: dismiss.fire()
    }
}
