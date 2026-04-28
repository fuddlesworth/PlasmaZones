// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
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
    property color highlightColor: Kirigami.Theme.highlightColor
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
    // Content-driven desired size, exposed for the unified host (which binds
    // its Window width/height to these readonly properties; C++ also reads
    // them after every property write to compute matching layer-shell margins).
    readonly property int contentDesiredWidth: container.width + Math.round(Kirigami.Units.gridUnit * 2.5)
    readonly property int contentDesiredHeight: container.height + Math.round(Kirigami.Units.gridUnit * 2.5)
    /// Idempotency latch for `dismissRequested`. The timer-fire path and the
    /// MouseArea click path can both attempt to dismiss within the same
    /// show cycle; without the latch, C++ runs Surface::hide() twice and the
    /// second call qCWarnings on the already-Hidden state. Reset on every
    /// dismissTimer.restart() via the Connections block below.
    property bool _dismissed: false

    /// Auto-dismiss request emitted by the dismissTimer / click MouseArea.
    /// The unified NotificationOverlay host re-emits this as its own
    /// `dismissRequested` so OverlayService::createWarmedOsdSurface's
    /// connect to Surface::hide() drives the library animator's beginHide.
    signal dismissRequested()

    /// Restart the auto-dismiss timer from C++ on every show. Replaces the
    /// QML-internal `dismissTimer.restart()` that the old show() used to
    /// call. Latch reset is driven off the timer's runningChanged
    /// transition (Connections block below) — that way any path that
    /// restarts the timer (this helper today, or a hypothetical future
    /// direct call) resets the latch automatically.
    function restartDismissTimer() {
        dismissTimer.restart();
    }

    /// Internal: emit dismissRequested at most once per show cycle.
    function _requestDismiss() {
        if (_dismissed)
            return ;

        _dismissed = true;
        root.dismissRequested();
    }

    Accessible.name: root.disabled ? root.disabledReason : i18n("Layout indicator")

    // Auto-dismiss timer. Emits a signal that C++ hooks to Surface::hide().
    Timer {
        id: dismissTimer

        interval: root.displayDuration
        onTriggered: root._requestDismiss()
    }

    // Reset the dismiss latch when the timer (re)starts. See the matching
    // block in NavigationOsdContent.qml for the rationale — the reset is
    // the timer's responsibility so any restart path keeps the latch in
    // sync without the helper having to remember to clear it.
    Connections {
        function onRunningChanged() {
            if (dismissTimer.running)
                root._dismissed = false;

        }

        target: dismissTimer
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

    // Main container
    Rectangle {
        id: container

        anchors.centerIn: parent
        width: previewContainer.width + Kirigami.Units.gridUnit * 3
        height: previewContainer.height + nameLabelRow.height + Kirigami.Units.gridUnit * 3
        color: Qt.rgba(root.backgroundColor.r, root.backgroundColor.g, root.backgroundColor.b, 0.95)
        radius: Kirigami.Units.gridUnit * 1.5
        border.color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.15)
        border.width: 1

        // Layout preview
        Item {
            id: previewContainer

            anchors.top: parent.top
            anchors.topMargin: Kirigami.Units.gridUnit * 1.5
            anchors.horizontalCenter: parent.horizontalCenter
            width: Kirigami.Units.gridUnit * 11
            height: Math.round(Kirigami.Units.gridUnit * 11 / root.previewAspectRatio)

            // Background for preview area
            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.08)
                radius: Kirigami.Units.smallSpacing
            }

            // Zone preview using shared component
            QFZCommon.ZonePreview {
                id: zonePreview

                anchors.fill: parent
                anchors.margins: 4
                zones: root.zones
                isHovered: false
                isActive: true
                zonePadding: 2
                edgeGap: 2
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
                animationDuration: 150
            }

        }

        // Lock overlay (shown on top of preview when locked — mutually exclusive with disabled)
        Rectangle {
            anchors.fill: previewContainer
            visible: root.locked && !root.disabled
            color: Qt.rgba(0, 0, 0, 0.5)
            radius: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                anchors.centerIn: parent
                source: "object-locked"
                width: Kirigami.Units.iconSizes.large
                height: Kirigami.Units.iconSizes.large
                color: Kirigami.Theme.highlightedTextColor
            }

        }

        // Disabled overlay (shown when context is disabled for this desktop/screen)
        Rectangle {
            anchors.fill: previewContainer
            visible: root.disabled
            color: Qt.rgba(0, 0, 0, 0.5)
            radius: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                anchors.centerIn: parent
                source: "dialog-cancel"
                width: Kirigami.Units.iconSizes.large
                height: Kirigami.Units.iconSizes.large
                color: Kirigami.Theme.neutralTextColor
            }

        }

        // Layout name with category badge
        Row {
            id: nameLabelRow

            anchors.top: previewContainer.bottom
            anchors.topMargin: Kirigami.Units.gridUnit
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: Kirigami.Units.gridUnit * 1.5
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

                anchors.verticalCenter: parent.verticalCenter
                text: root.disabled ? root.disabledReason : (root.locked ? i18n("%1 (Locked)", root.layoutName) : root.layoutName)
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.2
                font.weight: Font.Medium
                color: root.textColor
                horizontalAlignment: Text.AlignHCenter
            }

        }

    }

    // Click to dismiss. _requestDismiss collapses timer-fire + click into
    // a single dismissRequested per show cycle.
    MouseArea {
        anchors.fill: parent
        onClicked: root._requestDismiss()
    }

}
