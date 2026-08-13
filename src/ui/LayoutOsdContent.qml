// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Layout OSD content — Item-rooted body for use inside the
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
 *   - Data properties written by C++ (layoutId, zones, locked, …)
 *   - The visible content tree (zone preview + lock/disabled overlays + name row)
 *   - The auto-dismiss Timer + dismissRequested signal, forwarded by the
 *     shell host as `osdDismissRequested` and routed by C++ to
 *     OverlayService::onOsdDismissRequested → ShellHost::hideSlot
 */
Item {
    id: root

    // Layout data
    property string layoutId: ""
    property string layoutName: ""
    property var zones: []
    // Layout category, matching LayoutCategory in C++ and the vocabulary
    // CategoryBadge renders: 0=Manual, 1=Autotile, 2=ScrollingTemplate.
    property int category: 0
    // Per-layout autoAssign flag (raw, not yet OR'd with the global master
    // toggle). CategoryBadge folds in `globalAutoAssign` to display effective
    // state — see selector_update.cpp / snapassist.cpp / osd.cpp where these
    // properties are written.
    property bool autoAssign: false
    // Mirrors the global "Auto-assign for all layouts" master toggle (#370).
    // Forwarded into CategoryBadge so the badge shows effective state even
    // when the per-layout flag is off.
    property bool globalAutoAssign: false
    // Autotile algorithm metadata
    property bool showMasterDot: false
    property int masterCount: 1
    property bool producesOverlappingZones: false
    property string zoneNumberDisplay: "all"
    // Screen info for aspect ratio (bounded to prevent layout issues)
    property real screenAspectRatio: 16 / 9
    readonly property real safeAspectRatio: Math.max(0.5, Math.min(4, screenAspectRatio))
    // Layout's intended aspect ratio class (set from C++)
    property string aspectRatioClass: "any"
    // Resolved preview AR: use layout's class if set, fall back to screen's AR
    readonly property real previewAspectRatio: {
        switch (aspectRatioClass) {
        case "standard":
            return 16 / 9;
        case "ultrawide":
            return 21 / 9;
        case "super-ultrawide":
            return 32 / 9;
        case "portrait":
            return 9 / 16;
        default:
            return safeAspectRatio;
        }
    }
    // Auto-dismiss interval — a local constant (readonly: no C++ path
    // writes it and the shell does not forward it). Show/hide fade shapes
    // are owned by the SurfaceAnimator's `osd.show` / `osd.pop` /
    // `osd.hide` profile JSONs; tune the JSONs to adjust the
    // appear/disappear feel rather than re-introducing per-window duration
    // overrides here.
    readonly property int displayDuration: 1500
    // Theme colors
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightColor: QFZCommon.ZoneColorDefaults.previewActiveZoneColor
    property color inactiveColor: QFZCommon.ZoneColorDefaults.previewInactiveZoneColor
    property color borderColor: QFZCommon.ZoneColorDefaults.previewZoneBorderColor
    // Zone fill opacities for the preview. Written by C++ (osd.cpp
    // pushLayoutOsdContent) with the settings/context-override-resolved
    // values; these literals are the QML-side defaults.
    property real activeOpacity: 0.6
    property real inactiveOpacity: 0.3
    // Font properties for zone number labels
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property bool locked: false
    // True when the layout shown is a scrolling screen's sizing TEMPLATE
    // (live-Templates capability): the name label captions it "Column
    // template" so a template pick never reads as a snap-layout switch.
    property bool isTemplate: false
    property bool disabled: false
    property string disabledReason: ""
    // Icon for the disabled-style overlay card. This card is refusal-only
    // by design (overlayservice.h documents it: every producer explains why
    // a requested change had no effect), so the sole writer restates
    // "dialog-cancel" per show and the grey tint below is unconditional. A
    // future positive announcement must NOT reuse this card — it renders
    // its own content type instead (the scrolling mode switch, which
    // briefly reused it, now shows its strip preview).
    property string disabledIcon: "dialog-cancel"
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

    // StaticText role so the name is actually exposed (a bare name on a
    // roleless Item may never surface), and the name carries the CONTENT —
    // the layout name with its Locked / Column template decorations — not
    // just "an OSD appeared". Mirrors NavigationOsdContent's root.
    Accessible.role: Accessible.StaticText
    Accessible.name: nameLabel.text

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
    // fly-in / etc. instead of snapping in when the leg ends.
    QFZCommon.PopupFrame {
        id: container

        anchors.centerIn: parent
        // Grow to fit whichever is wider — the preview or the name row — so a
        // long layout name (e.g. "Portrait Master + Stack") doesn't overflow the
        // frame. The name label is capped + elided below, so the OSD widens only
        // up to that cap rather than without bound.
        width: Math.max(previewContainer.width, nameLabelRow.width) + Kirigami.Units.gridUnit * 3
        height: previewContainer.height + nameLabelRow.height + Kirigami.Units.gridUnit * 3
        backgroundColor: root.backgroundColor

        // Layout preview
        Item {
            id: previewContainer

            anchors.top: parent.top
            anchors.topMargin: Kirigami.Units.gridUnit * 1.5
            anchors.horizontalCenter: parent.horizontalCenter
            width: Kirigami.Units.gridUnit * 11
            height: Math.round(Kirigami.Units.gridUnit * 11 / root.previewAspectRatio)
            // The 16 px minZoneSize below INFLATES sub-floor zones, and a
            // dense layout's inflated zones can extend past this fixed-size
            // container — keep the overflow inside the card frame.
            clip: true

            // Background for preview area. backgroundColor (not the
            // alternate role) so inactive zone fills stay readable
            // against the backdrop.
            Rectangle {
                anchors.fill: parent
                color: Kirigami.Theme.backgroundColor
                radius: Kirigami.Units.smallSpacing
            }

            // Zone preview using shared component
            QFZCommon.ZonePreview {
                id: zonePreview

                anchors.fill: parent
                anchors.margins: Kirigami.Units.smallSpacing
                zones: root.zones
                highlightColor: root.highlightColor
                inactiveColor: root.inactiveColor
                borderColor: root.borderColor
                isHovered: false
                isActive: true
                zonePadding: Math.round(Kirigami.Units.smallSpacing / 2)
                edgeGap: Math.round(Kirigami.Units.smallSpacing / 2)
                // 16, not lower: ZonePreview hides zone numbers under a
                // hard 16 px legibility floor, and this instance shows them
                // — a smaller floor renders 12-15 px zones as silent
                // unnumbered boxes (MonitorStatePage raises its floor for
                // the same coupling).
                minZoneSize: 16
                showZoneNumbers: true
                producesOverlappingZones: root.producesOverlappingZones
                zoneNumberDisplay: root.zoneNumberDisplay
                inactiveOpacity: root.inactiveOpacity
                activeOpacity: root.activeOpacity
                fontFamily: root.fontFamily
                fontSizeScale: root.fontSizeScale
                fontWeight: root.fontWeight
                fontItalic: root.fontItalic
                fontUnderline: root.fontUnderline
                showMasterDot: root.showMasterDot
                masterCount: root.masterCount
                fontStrikeout: root.fontStrikeout
                animationDuration: Kirigami.Units.shortDuration
            }
        }

        // Lock overlay (shown on top of preview when locked — mutually exclusive with disabled)
        Rectangle {
            anchors.fill: previewContainer
            visible: root.locked && !root.disabled
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.5)
            radius: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                anchors.centerIn: parent
                source: "object-locked"
                width: Kirigami.Units.iconSizes.large
                height: Kirigami.Units.iconSizes.large
                color: Kirigami.Theme.textColor
            }
        }

        // Disabled overlay (shown when context is disabled for this desktop/screen)
        Rectangle {
            anchors.fill: previewContainer
            visible: root.disabled
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.5)
            radius: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                anchors.centerIn: parent
                source: root.disabledIcon
                width: Kirigami.Units.iconSizes.large
                height: Kirigami.Units.iconSizes.large
                // Unconditional grey: the card is refusal-only (see the
                // disabledIcon note above), so the tint always applies.
                color: Kirigami.Theme.disabledTextColor
            }
        }

        // Layout name with category badge
        Row {
            id: nameLabelRow

            anchors.top: previewContainer.bottom
            anchors.topMargin: Kirigami.Units.gridUnit
            anchors.horizontalCenter: parent.horizontalCenter
            // Vertical padding budget lives in the container height sum (3 gu total): 1.5 gu top, 1.0 gu gap above this row, 0.5 gu below it.
            spacing: Kirigami.Units.smallSpacing

            // Category badge (layout type) — hidden when disabled
            QFZCommon.CategoryBadge {
                id: categoryBadge

                visible: !root.disabled
                anchors.verticalCenter: parent.verticalCenter
                category: root.category
                autoAssign: root.autoAssign
                globalAutoAssign: root.globalAutoAssign
            }

            Label {
                id: nameLabel

                // The root Item announces this text (Accessible.name binds
                // nameLabel.text); without ignoring the label a screen
                // reader walks the same text twice.
                Accessible.ignored: true
                // Cap the name width so a long layout name widens the OSD only up
                // to this bound, then elides with "…" instead of spilling past the
                // frame. Short names use their natural width (full text shown).
                // The disabled card gets a wider cap: its text is a SENTENCE
                // built from user data ("Disabled on <desktop name>"), it is
                // the only thing the card carries (the preview is blanked),
                // and the name-sized cap elided exactly the words that
                // identify the refusal.
                readonly property int maxWidth: root.disabled ? Kirigami.Units.gridUnit * 24 : Kirigami.Units.gridUnit * 16
                // Kirigami exposes no OSD-headline type constant; same named
                // factor the nav OSD documents (messageFontScale there).
                readonly property real nameFontScale: 1.2

                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (root.disabled)
                        return root.disabledReason;
                    var name = root.isTemplate ? i18nc("OSD caption, %1 is the template name", "Column template — %1", root.layoutName) : root.layoutName;
                    return root.locked ? i18n("%1 (Locked)", name) : name;
                }
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * nameFontScale
                font.weight: Font.Medium
                color: root.textColor
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                width: Math.min(implicitWidth, maxWidth)
            }
        }
    }

    // Click the card to dismiss — BEST-EFFORT only: the OSD's host surface
    // is input-transparent whenever no modal slot is up (see the
    // anyInputGrabbing rationale in shellhost_bridge.cpp), so in the common
    // case this area receives nothing and the timer is the real dismiss.
    // Clicks land here only while a modal slot has the surface accepting
    // input; the card anchoring keeps the modal's own clicks out of a
    // screen-wide shield in that case. dismiss.fire() collapses timer-fire +
    // click into a single dismissRequested per show cycle via the shared
    // latch. Keep the MouseArea: the daemon rationale depends on it existing
    // for the modal-visible case.
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
