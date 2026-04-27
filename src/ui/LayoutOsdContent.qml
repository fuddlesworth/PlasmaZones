// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.plasmazones.common as QFZCommon

/**
 * Layout OSD content — Item-rooted body for use either inside the standalone
 * LayoutOsd Window or inside the unified NotificationOverlay host that swaps
 * OSD/picker modes via Loader.
 *
 * Owns the layout-preview toast's data properties, animations, dismiss timer,
 * show/hide functions, and content tree. The hosting Window is a thin shell
 * that forwards C++ property writes via aliases and binds
 * Qt.WindowTransparentForInput to the public `dismissed` state.
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
    // Timing
    property int displayDuration: 1500
    // ms before auto-hide. Show/hide fade shapes are driven by the
    // "osd.show" / "osd.hide" / "osd.pop" profile JSONs — tune those
    // to adjust the appear/disappear feel rather than re-introducing
    // per-window duration overrides here.
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
    property string disabledReason
    // Per-instance fade overrides forwarded from the host Window.
    property int fadeInDuration: -1
    property int fadeOutDuration: -1
    // Public dismiss state used by the hosting Window's flags binding to
    // toggle Qt.WindowTransparentForInput. True while the OSD is logically
    // hidden (pre-first-show or after a full hide animation), cleared by
    // show(), set again at the end of hideAnimation. NOT tied to opacity —
    // discrete flip on show/dismiss only, so the host's input-region flag
    // doesn't churn during the fade.
    property bool dismissed: true
    // Content-driven desired size, exposed for the hosting Window (or Loader
    // host) to size its layer surface to fit the rendered content.
    readonly property int contentDesiredWidth: container.width + Math.round(Kirigami.Units.gridUnit * 2.5)
    readonly property int contentDesiredHeight: container.height + Math.round(Kirigami.Units.gridUnit * 2.5)

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

    // After L3 v2, the host Window stays visible across hide cycles to
    // preserve the Wayland layer-shell wl_surface and Vulkan swap chain.
    // We gate on `dismissed` instead: if we're already in the dismissed
    // state, the hide animation is a no-op (another hide() mid-dismiss
    // would just re-run the same fade-out against already-zero opacity).
    // Also guards the pre-first-show case: warm-upped windows start with
    // dismissed == true, so an errant hide() before any show() short-
    // circuits cleanly.
    function hide() {
        if (root.dismissed)
            return ;

        showAnimation.stop();
        dismissTimer.stop();
        hideAnimation.start();
    }

    // Animated by show/hide animations. Item.opacity works on Wayland where
    // Window.opacity does not — the previous standalone LayoutOsd.qml wrapped
    // the content in a contentWrapper Item for exactly this reason; now that
    // the root IS that Item, the wrapper layer is gone.
    opacity: 0
    Accessible.name: root.disabled ? root.disabledReason : i18n("Layout indicator")

    // Auto-dismiss timer
    Timer {
        id: dismissTimer

        interval: root.displayDuration
        onTriggered: root.hide()
    }

    // Show animation. Opacity uses osd.show (plain OutCubic decel).
    // Scale uses osd.pop (OutBack overshoot) to preserve the subtle
    // "pop" the original design used — matching the pre-PhosphorMotion
    // NumberAnimation { easing.type: Easing.OutBack; overshoot: 1.2 }
    // shape. Do not collapse both onto a single profile.
    // durationOverride binds to root.fadeInDuration / fadeOutDuration so
    // consumers that override those properties still drive the timing.
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

    // Click to dismiss. Gated on dismissed so that in the post-hide
    // transparent state we don't consume events at the Qt Quick level
    // even if the wl_surface input region hasn't been updated yet.
    MouseArea {
        anchors.fill: parent
        enabled: !root.dismissed
        onClicked: root.hide()
    }

}
