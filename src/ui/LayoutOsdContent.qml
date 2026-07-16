// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Layout OSD content — Item-rooted body for use inside the unified
 * NotificationOverlay host that swaps OSD modes via Loader.
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
 *   - The auto-dismiss Timer + dismissRequested signal that C++ wires to
 *     Surface::hide() (via the unified host's signal forwarding)
 */
Item {
    id: root

    // Layout data
    property string layoutId: ""
    property string layoutName: ""
    property var zones: []
    // Layout category: 0=Manual (matches LayoutCategory in C++)
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
    // Auto-dismiss interval. Show/hide fade shapes are owned by the
    // SurfaceAnimator's `osd.show` / `osd.pop` / `osd.hide` profile JSONs;
    // tune the JSONs to adjust the appear/disappear feel rather than
    // re-introducing per-window duration overrides here.
    property int displayDuration: 1500
    // Theme colors
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color textColor: Kirigami.Theme.textColor
    property color highlightColor: QFZCommon.ZoneColorDefaults.previewActiveZoneColor
    property color inactiveColor: QFZCommon.ZoneColorDefaults.previewInactiveZoneColor
    property color borderColor: QFZCommon.ZoneColorDefaults.previewZoneBorderColor
    // Font properties for zone number labels
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property bool locked: false
    property bool disabled: false
    property string disabledReason: ""
    /// Auto-dismiss request emitted by the dismissTimer / click MouseArea.
    /// The unified NotificationOverlay host re-emits this as its own
    /// `dismissRequested` so OverlayService::createWarmedOsdSurface's
    /// connect to Surface::hide() drives the library animator's beginHide.
    signal dismissRequested

    /// Restart the auto-dismiss timer from C++ on every show. Forwards to
    /// the shared OsdDismissable helper so the latch reset is driven off
    /// the timer's runningChanged transition automatically.
    function restartDismissTimer() {
        dismiss.restart();
    }

    Accessible.name: root.disabled ? root.disabledReason : i18n("Layout indicator")

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
        containerRadius: Kirigami.Units.gridUnit * 1.5

        // Layout preview
        Item {
            id: previewContainer

            anchors.top: parent.top
            anchors.topMargin: Kirigami.Units.gridUnit * 1.5
            anchors.horizontalCenter: parent.horizontalCenter
            width: Kirigami.Units.gridUnit * 11
            height: Math.round(Kirigami.Units.gridUnit * 11 / root.previewAspectRatio)

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
                minZoneSize: 12
                showZoneNumbers: true
                producesOverlappingZones: root.producesOverlappingZones
                zoneNumberDisplay: root.zoneNumberDisplay
                inactiveOpacity: 0.3
                activeOpacity: 0.6
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
                source: "dialog-cancel"
                width: Kirigami.Units.iconSizes.large
                height: Kirigami.Units.iconSizes.large
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
                autoAssign: root.autoAssign === true
                globalAutoAssign: root.globalAutoAssign === true
            }

            Label {
                id: nameLabel

                // Cap the name width so a long layout name widens the OSD only up
                // to this bound, then elides with "…" instead of spilling past the
                // frame. Short names use their natural width (full text shown).
                readonly property int maxWidth: Kirigami.Units.gridUnit * 16

                anchors.verticalCenter: parent.verticalCenter
                text: root.disabled ? root.disabledReason : (root.locked ? i18n("%1 (Locked)", root.layoutName) : root.layoutName)
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.2
                font.weight: Font.Medium
                color: root.textColor
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                width: Math.min(implicitWidth, maxWidth)
            }
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
        Accessible.name: i18n("Dismiss notification")
    }
}
